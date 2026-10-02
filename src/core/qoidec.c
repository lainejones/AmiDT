/* qoidec.c - QOI, see moredec.h */
#include "moredec.h"

static int get(png_read_fn rd, void *h, unsigned char *b, long n) { return rd(h, b, n) == n; }

struct qin { png_read_fn rd; void *h; unsigned char buf[1024]; int pos, len; };

static int refill(struct qin *p)                /* 0 at the end */
{
    p->len = (int)p->rd(p->h, p->buf, sizeof(p->buf));
    p->pos = 0;
    if (p->len <= 0) { p->len = 0; return 0; }
    return 1;
}

/* the next byte into v, or -1 at the end; ip/ie are the decoder's copy of the buffer position */
#define QB(v) do { if (ip >= ie) { if (!refill(in)) { v = -1; break; } ip = in->buf; ie = in->buf + in->len; } v = *ip++; } while (0)

int qoi_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    unsigned char hd[14], ix[64][4], px[4], *rgb = 0, *idx = 0;
    struct qin *in = 0;
    unsigned long w, ht;
    int i, rc = PNG_OK, run = 0, y, x, trunc = 0;
    if (!get(rd, h, hd, 14) || hd[0] != 'q' || hd[1] != 'o' || hd[2] != 'i' || hd[3] != 'f') return PNG_NOTPNG;
    w = ((unsigned long)hd[4] << 24) | ((unsigned long)hd[5] << 16) | ((unsigned long)hd[6] << 8) | hd[7];
    ht = ((unsigned long)hd[8] << 24) | ((unsigned long)hd[9] << 16) | ((unsigned long)hd[10] << 8) | hd[11];
    if (!w || !ht || w > 32767 || ht > 32767 || (hd[12] != 3 && hd[12] != 4)) return PNG_BADDATA;
    o->w = (int)w; o->h = (int)ht; o->ncolours = Q_COLOURS; o->depth = 8;
    for (i = 0; i < 256; i++) { o->pal[i][0] = q->pal[i][0]; o->pal[i][1] = q->pal[i][1]; o->pal[i][2] = q->pal[i][2]; }
    if (!o->begin(o)) return PNG_ABORT;
    in = al(sizeof(*in)); rgb = al(w * 3); idx = al(w);
    if (!in || !rgb || !idx) { rc = PNG_NOMEM; goto out; }
    in->rd = rd; in->h = h; in->pos = in->len = 0;
    for (i = 0; i < 64; i++) ix[i][0] = ix[i][1] = ix[i][2] = ix[i][3] = 0;
    px[0] = px[1] = px[2] = 0; px[3] = 255;
    {
        const unsigned char *ip = in->buf, *ie = in->buf;
        for (y = 0; y < (int)ht; y++) {
            unsigned char *d = rgb;
            for (x = 0; x < (int)w; x++, d += 3) {
                int b;
                if (run) { run--; d[0] = px[0]; d[1] = px[1]; d[2] = px[2]; continue; }
                if (trunc) { d[0] = px[0]; d[1] = px[1]; d[2] = px[2]; continue; }
                QB(b);
                if (b < 0) trunc = 1;           /* truncated: the rest stays the last colour */
                else if (b >= 0xFE) {
                    int k, n = b == 0xFE ? 3 : 4, v;
                    for (k = 0; k < n; k++) { QB(v); if (v < 0) { trunc = 1; break; } px[k] = (unsigned char)v; }
                } else switch (b >> 6) {
                case 0: px[0] = ix[b][0]; px[1] = ix[b][1]; px[2] = ix[b][2]; px[3] = ix[b][3]; break;
                case 1:
                    px[0] = (unsigned char)(px[0] + ((b >> 4) & 3) - 2);
                    px[1] = (unsigned char)(px[1] + ((b >> 2) & 3) - 2);
                    px[2] = (unsigned char)(px[2] + (b & 3) - 2);
                    break;
                case 2: {
                    int b2, dg = (b & 63) - 32;
                    QB(b2);
                    if (b2 < 0) { trunc = 1; break; }
                    px[0] = (unsigned char)(px[0] + dg - 8 + ((b2 >> 4) & 15));
                    px[1] = (unsigned char)(px[1] + dg);
                    px[2] = (unsigned char)(px[2] + dg - 8 + (b2 & 15));
                    break;
                }
                case 3: run = b & 63; break;    /* this pixel + run more */
                }
                if (b >> 6 != 0 || b >= 0xFE) { /* (an index hit is already in the table) */
                    int hh = (px[0] * 3 + px[1] * 5 + px[2] * 7 + px[3] * 11) & 63;
                    ix[hh][0] = px[0]; ix[hh][1] = px[1]; ix[hh][2] = px[2]; ix[hh][3] = px[3];
                }
                d[0] = px[0]; d[1] = px[1]; d[2] = px[2];
            }
            if (trunc && !y) { rc = PNG_BADDATA; goto out; }
            q_row(q, rgb, 3, (int)w, y, idx);
            o->row(o, y, idx);
        }
    }
out:
    if (idx) fr(idx);
    if (rgb) fr(rgb);
    if (in) fr(in);
    return rc;
}
