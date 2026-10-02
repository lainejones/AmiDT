/* inflate.h - a small streaming zlib/deflate decoder (RFC 1950/1951) for AmiDT.
 *
 * Pull-style: zin_read() fills exactly the number of bytes asked for (one PNG scanline at a
 * time), keeping its place between calls, so a picture never has to be decompressed whole.
 * The compressed bytes come from a callback (the PNG reader stitches IDAT chunks together).
 * Huffman codes are decoded through a 9-bit lookup table with a canonical slow path for the
 * longer ones.  Portable C: the Amiga datatype and the PC test harness build the same file.
 */
#ifndef AMIDT_INFLATE_H
#define AMIDT_INFLATE_H

typedef long (*zin_src)(void *h, unsigned char *buf, long max);   /* <= 0: no more input */

#define ZIN_FAST 10
#define ZIN_WSIZE 32768
#define ZIN_BUF (2 * ZIN_WSIZE)           /* output runs linearly through this, then slides down */

struct zin_huff {
    unsigned short fast[1 << ZIN_FAST];   /* (sym << 4) | len, 0 = use the slow path */
    unsigned short count[16];             /* codes of each length */
    unsigned short sym[320];              /* symbols by code */
};

struct zin {
    zin_src src;
    void *h;
    unsigned char in[4096];
    long inpos, inlen;
    unsigned long bits;                   /* bit buffer, LSB first */
    int nbits;
    int state;                            /* 0 block header, 1 stored, 2 huffman, 3 done, -1 error */
    int final;
    long stored;                          /* bytes left in a stored block */
    struct zin_huff lit, dist;
    long wp, rp;                          /* written / handed out, in buf */
    unsigned char buf[ZIN_BUF + 264];     /* the last 32 KB of output (the window) + what's new; a match may overrun the end by 258 */
};

/* zlib header included (PNG): skips the 2 header bytes; 0 = bad header */
int  zin_init(struct zin *z, zin_src src, void *h);
/* exactly n bytes into dst; returns n, or less at the end of the data / on an error (z->state) */
long zin_read(struct zin *z, unsigned char *dst, long n);

#endif
