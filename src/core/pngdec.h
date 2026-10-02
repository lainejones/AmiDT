/* pngdec.h - a streaming PNG decoder for AmiDT that hands out finished 8-bit rows.
 *
 * Palette, greyscale and 1/2/4-bit images keep their own colours (no dithering); truecolor
 * (with or without alpha, 8 or 16 bits) goes through quant.c.  Non-interlaced images are
 * decoded a row at a time - no full-size buffer at all; Adam7 images collect into one
 * w*h byte buffer first.  Alpha and tRNS are ignored (the picture is shown opaque).
 */
#ifndef AMIDT_PNGDEC_H
#define AMIDT_PNGDEC_H

#include "quant.h"

typedef long  (*png_read_fn)(void *h, unsigned char *buf, long n);     /* the file */
typedef void *(*png_alloc_fn)(unsigned long n);                        /* zeroed memory */
typedef void  (*png_free_fn)(void *p);

struct png_out {
    int w, h;
    int depth;                          /* 1..8 planes the picture needs */
    int ncolours;
    unsigned char pal[256][3];
    /* called once with the size and palette known, before any row; nonzero = go on */
    int  (*begin)(struct png_out *o);
    void (*row)(struct png_out *o, int y, const unsigned char *idx);
    /* optional, depth 1 only: a row that is already a bitplane row (MSB = leftmost pixel) */
    void (*row1)(struct png_out *o, int y, const unsigned char *bits);
    /* optional: the palette changed after begin() (PCX keeps its 256 colours at the end of the file) */
    void (*colours)(struct png_out *o);
    void *user;
};

enum { PNG_OK = 0, PNG_NOTPNG, PNG_BADDATA, PNG_NOMEM, PNG_UNSUPPORTED, PNG_ABORT };

int png_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o);

#endif
