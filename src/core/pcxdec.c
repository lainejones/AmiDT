/* pcxdec.c - ZSoft PCX, see moredec.h */
#include "moredec.h"

static int get(png_read_fn rd, void *h, unsigned char *b, long n) { return rd(h, b, n) == n; }

/* the RLE stream, one scanline (planes * bytesperline bytes) at a time; runs may cross lines */
struct pcx_in {
    png_read_fn rd; void *h;
    unsigned char buf[512]; int pos, len;
    int run; unsigned char val;
};

static int byte(struct pcx_in *p, unsigned char *b)
{
    if (p->pos >= p->len) {
        p->len = (int)p->rd(p->h, p->buf, sizeof(p->buf));
        p->pos = 0;
        if (p->len <= 0) { p->len = 0; return 0; }
    }
    *b = p->buf[p->pos++];
    return 1;
}

static int line(struct pcx_in *p, unsigned char *dst, long n, int rle)
{
    const unsigned char *ip = p->buf + p->pos, *ie = p->buf + p->len;
    unsigned char *end = dst + n;
    while (dst < end) {
        unsigned char c;
        if (p->run) {
            long k = p->run < end - dst ? p->run : end - dst;
            p->run -= (int)k;
            while (k--) *dst++ = p->val;
            continue;
        }
        if (ip >= ie) {
            p->pos = p->len;
            if (!byte(p, &c)) { while (dst < end) *dst++ = 0; return 0; }   /* truncated: the rest black */
            ip = p->buf + p->pos; ie = p->buf + p->len;
        } else c = *ip++;
        if (rle && (c & 0xC0) == 0xC0) {
            p->run = c & 0x3F;
            if (ip >= ie) {
                p->pos = p->len;
                if (!byte(p, &p->val)) { while (dst < end) *dst++ = 0; return 0; }
                ip = p->buf + p->pos; ie = p->buf + p->len;
            } else p->val = *ip++;
        } else *dst++ = c;
    }
    p->pos = (int)(ip - p->buf);
    return 1;
}

int pcx_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    unsigned char hd[128], *sl = 0, *idx = 0;
    struct pcx_in *in = 0;
    int bpp, np, bpl, w, ht, i, rc = PNG_OK, y, x, rle, trunc = 0;
    long sll;
    if (!get(rd, h, hd, 128)) return PNG_NOTPNG;
    if (hd[0] != 0x0A || hd[2] > 1) return PNG_NOTPNG;
    rle = hd[2] == 1;
    bpp = hd[3]; np = hd[65]; bpl = hd[66] | (hd[67] << 8);
    w = (hd[8] | (hd[9] << 8)) - (hd[4] | (hd[5] << 8)) + 1;
    ht = (hd[10] | (hd[11] << 8)) - (hd[6] | (hd[7] << 8)) + 1;
    if (w <= 0 || ht <= 0 || bpl <= 0 || (long)bpl * 8 < (long)w * (bpp == 8 ? 8 : bpp)) return PNG_BADDATA;
    if (!((bpp == 1 && (np == 1 || np == 2 || np == 3 || np == 4)) || ((bpp == 2 || bpp == 4) && np == 1) ||
          (bpp == 8 && (np == 1 || np == 3 || np == 4)))) return PNG_UNSUPPORTED;
    o->w = w; o->h = ht;
    if (bpp == 8 && np >= 3) {
        o->ncolours = Q_COLOURS; o->depth = 8;
        for (i = 0; i < 256; i++) { o->pal[i][0] = q->pal[i][0]; o->pal[i][1] = q->pal[i][1]; o->pal[i][2] = q->pal[i][2]; }
    } else if (bpp == 8) {                      /* the real palette comes after the image: grey until then */
        o->ncolours = 256; o->depth = 8;
        for (i = 0; i < 256; i++) o->pal[i][0] = o->pal[i][1] = o->pal[i][2] = (unsigned char)i;
    } else if (bpp == 1 && np == 1) {           /* mono: black and white (headers often hold junk) */
        o->ncolours = 2; o->depth = 1;
        o->pal[0][0] = o->pal[0][1] = o->pal[0][2] = 0;
        o->pal[1][0] = o->pal[1][1] = o->pal[1][2] = 255;
    } else {                                    /* the 16-colour header palette */
        int bits = bpp * np;
        o->ncolours = 1 << bits; o->depth = bits;
        for (i = 0; i < 16; i++) { o->pal[i][0] = hd[16 + i * 3]; o->pal[i][1] = hd[17 + i * 3]; o->pal[i][2] = hd[18 + i * 3]; }
    }
    if (!o->begin(o)) return PNG_ABORT;
    sll = (long)bpl * np;
    in = al(sizeof(*in));
    sl = al((unsigned long)sll);
    idx = al((unsigned long)w + 8);
    if (!in || !sl || !idx) { rc = PNG_NOMEM; goto out; }
    in->rd = rd; in->h = h; in->pos = in->len = 0; in->run = 0;
    for (y = 0; y < ht; y++) {
        if (!line(in, sl, sll, rle)) trunc = 1; /* truncated: this line as far as it came, then black */
        else if (trunc) break;
        if (bpp == 8 && np == 1) { o->row(o, y, sl); continue; }
        if (bpp == 8) {                         /* R, G, B (, A) planes of one line each */
            const unsigned char *r = sl, *g = sl + bpl, *b = sl + 2 * bpl;
            int cy = (y & 3) << 2;
            for (x = 0; x < w; x++) {
                int c = cy | (x & 3);
                idx[x] = (unsigned char)(q->r[c][r[x]] + q->g[c][g[x]] + q->b[c][b[x]]);
            }
        } else if (np == 1 && bpp == 1) {
            if (o->row1) { o->row1(o, y, sl); continue; }
            for (x = 0; x < w; x++) idx[x] = (unsigned char)((sl[x >> 3] >> (7 - (x & 7))) & 1);
        } else if (np == 1) {                   /* 2 or 4 bits packed */
            int per = 8 / bpp, mask = (1 << bpp) - 1;
            for (x = 0; x < w; x++) idx[x] = (unsigned char)((sl[x / per] >> ((per - 1 - x % per) * bpp)) & mask);
        } else {                                /* 1-bit planes */
            for (x = 0; x < w; x++) {
                int p, v = 0;
                for (p = 0; p < np; p++) v |= ((sl[p * bpl + (x >> 3)] >> (7 - (x & 7))) & 1) << p;
                idx[x] = (unsigned char)v;
            }
        }
        o->row(o, y, idx);
        if (trunc) break;
    }
    if (trunc) {                                /* (0 = black in every format here) */
        for (x = 0; x < w; x++) idx[x] = 0;
        for (y++; y < ht; y++) o->row(o, y, idx);
    }
    if (bpp == 8 && np == 1 && !trunc) {                  /* 0x0C and 768 bytes of palette after the image */
        unsigned char c, *tail = sl;            /* the last 769 bytes of the file, round robin */
        unsigned long n = 0;
        if (sll < 769) { fr(sl); if (!(tail = sl = al(769))) goto out; }
        while (byte(in, &c)) tail[n++ % 769] = c;
        if (n >= 769 && tail[n % 769] == 0x0C) {
            for (i = 0; i < 768; i++) o->pal[i / 3][i % 3] = tail[(n + 1 + i) % 769];
            if (o->colours) o->colours(o);
        }
    }
out:
    if (idx) fr(idx);
    if (sl) fr(sl);
    if (in) fr(in);
    return rc;
}
