/* moredec.h - the smaller formats for AmiDT, as 8-bit rows (struct png_out):
 *
 *   TGA  - colour-mapped (8-bit), truecolor 15/16/24/32, grey; raw or RLE; any origin
 *   PCX  - 1-bit mono, 4-plane EGA, 2/4-bit packed, 8-bit (palette at the end of the file:
 *          rows stream first, the colours come after - png_out.colours), 24/32-bit planes
 *   ICO  - the biggest, deepest image of the icon: BMP (bmpdec) or PNG (pngdec) inside
 *   QOI  - "Quite OK Image" (qoiformat.org), RGB/RGBA
 *
 * Truecolor goes through quant.c; alpha is ignored (the picture is shown opaque).
 */
#ifndef AMIDT_MOREDEC_H
#define AMIDT_MOREDEC_H

#include "pngdec.h"

int tga_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);
int pcx_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);
int ico_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);
int qoi_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);

#endif
