/* gifdec.h - GIF (87a/89a) for AmiDT: the first image, as 8-bit rows (struct png_out).
 *
 * The picture is the logical screen; the first image sits on it at its own position over the
 * background colour.  Rows stream straight out as LZW produces them (no full-size buffer) unless
 * the image is interlaced or smaller than the screen; then one w*h byte buffer is used.
 * Transparency is ignored (the picture is shown opaque), as are later frames.
 */
#ifndef AMIDT_GIFDEC_H
#define AMIDT_GIFDEC_H

#include "pngdec.h"

int gif_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, struct png_out *o);

#endif
