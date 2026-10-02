/* quant.h - 24-bit -> 8-bit for AmiDT: a fixed 6x7x6 colour cube (252 colours) with a 4x4
 * ordered (Bayer) dither.
 *
 * This is the step that makes the AmiDT datatypes fast: OS 3.2's picture.datatype spends most
 * of a truecolor picture's time reducing it to the screen; done here instead it is three table
 * lookups and two additions per pixel, row by row, and the dither depends only on (x, y), so
 * rows can be produced in any order (interlaced PNGs).
 */
#ifndef AMIDT_QUANT_H
#define AMIDT_QUANT_H

#define Q_COLOURS 252                   /* 6 red x 7 green x 6 blue */

struct quant {
    unsigned char r[16][256], g[16][256], b[16][256];   /* per dither cell: value -> index part */
    unsigned char pal[256][3];                           /* the cube's colours */
};

void q_init(struct quant *q);
/* one row: rgb = 3 bytes a pixel (step = bytes between pixels: 3 RGB, 4 RGBA), y = the row */
void q_row(const struct quant *q, const unsigned char *rgb, int step, int w, int y, unsigned char *out);
void q_row_bgr(const struct quant *q, const unsigned char *bgr, int step, int w, int y, unsigned char *out);

#endif
