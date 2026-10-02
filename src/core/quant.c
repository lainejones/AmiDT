/* quant.c - see quant.h */
#include "quant.h"

static const unsigned char BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

/* the index part for one channel with `levels` steps (multiplier = the stride in the cube) */
static void channel(unsigned char t[16][256], int levels, int mult)
{
    int c, v;
    for (c = 0; c < 16; c++) {
        /* threshold in (0,1): ordered dither between the two nearest levels */
        long thr = BAYER[c] * 2 + 1;                          /* 1..31, out of 32 */
        for (v = 0; v < 256; v++) {
            long scaled = (long)v * (levels - 1) * 32;       /* position * 32 * 255 */
            long lo = scaled / (255 * 32);
            long frac = (scaled - lo * 255 * 32) / 255;       /* 0..31 */
            long lvl = lo + (frac >= thr ? 1 : 0);
            if (lvl > levels - 1) lvl = levels - 1;
            t[c][v] = (unsigned char)(lvl * mult);
        }
    }
}

void q_init(struct quant *q)
{
    int r, g, b, i = 0;
    channel(q->r, 6, 42);              /* index = r*42 + g*6 + b */
    channel(q->g, 7, 6);
    channel(q->b, 6, 1);
    for (r = 0; r < 6; r++)
        for (g = 0; g < 7; g++)
            for (b = 0; b < 6; b++, i++) {
                q->pal[i][0] = (unsigned char)(r * 255 / 5);
                q->pal[i][1] = (unsigned char)(g * 255 / 6);
                q->pal[i][2] = (unsigned char)(b * 255 / 5);
            }
    for (; i < 256; i++) q->pal[i][0] = q->pal[i][1] = q->pal[i][2] = 0;
}

void q_row(const struct quant *q, const unsigned char *rgb, int step, int w, int y, unsigned char *out)
{
    int x, row = (y & 3) << 2;
    for (x = 0; x < w; x++, rgb += step) {
        int c = row | (x & 3);
        out[x] = (unsigned char)(q->r[c][rgb[0]] + q->g[c][rgb[1]] + q->b[c][rgb[2]]);
    }
}

void q_row_bgr(const struct quant *q, const unsigned char *bgr, int step, int w, int y, unsigned char *out)
{
    int x, row = (y & 3) << 2;
    for (x = 0; x < w; x++, bgr += step) {
        int c = row | (x & 3);
        out[x] = (unsigned char)(q->r[c][bgr[2]] + q->g[c][bgr[1]] + q->b[c][bgr[0]]);
    }
}
