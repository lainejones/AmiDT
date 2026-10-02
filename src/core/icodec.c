/* icodec.c - Windows ICO, see moredec.h.  The file is read front to back only: the chosen image
 * must lie after the directory (it always does), and its first bytes are peeked and replayed. */
#include "moredec.h"
#include "bmpdec.h"

static int get(png_read_fn rd, void *h, unsigned char *b, long n) { return rd(h, b, n) == n; }
static unsigned long le32(const unsigned char *p) { return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }

struct replay { png_read_fn rd; void *h; unsigned char pb[8]; int n, pos; };

static long replay_rd(void *hh, unsigned char *b, long n)
{
    struct replay *r = hh;
    long got = 0;
    while (got < n && r->pos < r->n) b[got++] = r->pb[r->pos++];
    if (got < n) { long k = r->rd(r->h, b + got, n - got); if (k > 0) got += k; }
    return got;
}

#define MAXENT 64

int ico_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    unsigned char hd[6], e[16];
    struct replay r;
    unsigned long best_off = 0, pos, skipn;
    long best_score = -1;
    int n, i;
    if (!get(rd, h, hd, 6) || hd[0] || hd[1] || hd[2] != 1 || hd[3]) return PNG_NOTPNG;
    n = hd[4] | (hd[5] << 8);
    if (!n || n > MAXENT) return PNG_BADDATA;
    for (i = 0; i < n; i++) {                   /* the biggest, then the deepest */
        long w, ht, bpp, score;
        if (!get(rd, h, e, 16)) return PNG_BADDATA;
        w = e[0] ? e[0] : 256; ht = e[1] ? e[1] : 256;
        bpp = e[6] | (e[7] << 8);
        if (!bpp) bpp = e[2] ? (e[2] <= 2 ? 1 : e[2] <= 16 ? 4 : 8) : 8;
        score = w * ht * 64 + bpp;
        if (score > best_score) { best_score = score; best_off = le32(e + 12); }
    }
    pos = 6 + 16UL * n;
    if (best_off < pos) return PNG_BADDATA;
    for (skipn = best_off - pos; skipn; ) {
        unsigned char tmp[256];
        long k = skipn > sizeof(tmp) ? (long)sizeof(tmp) : (long)skipn;
        if (!get(rd, h, tmp, k)) return PNG_BADDATA;
        skipn -= (unsigned long)k;
    }
    r.rd = rd; r.h = h; r.pos = 0;
    if (!get(rd, h, r.pb, 8)) return PNG_BADDATA;
    r.n = 8;
    if (r.pb[0] == 137 && r.pb[1] == 'P' && r.pb[2] == 'N' && r.pb[3] == 'G')
        return png_decode(replay_rd, &r, al, fr, q, o);
    return bmp_decode_ico(replay_rd, &r, al, fr, q, o);
}
