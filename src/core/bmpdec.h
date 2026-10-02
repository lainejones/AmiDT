/* bmpdec.h - Windows/OS2 BMP for AmiDT, as 8-bit rows (struct png_out).
 *
 * 1/4/8-bit palette (plain, RLE4, RLE8), 16/24/32-bit (plain or BITFIELDS) through quant.c.
 * Bottom-up files hand their rows out bottom-up - the bitmap takes rows in any order, so
 * nothing is buffered beyond one row.  Alpha is ignored.
 */
#ifndef AMIDT_BMPDEC_H
#define AMIDT_BMPDEC_H

#include "pngdec.h"

int bmp_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);
/* an icon's BMP image (ICO): no file header, height includes the AND mask */
int bmp_decode_ico(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);

#endif
