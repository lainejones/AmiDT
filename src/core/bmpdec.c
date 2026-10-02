/* bmpdec.c - see bmpdec.h */
#include "bmpdec.h"

static int get(png_read_fn rd, void *h, unsigned char *b, long n) { return rd(h, b, n) == n; }
static unsigned long le32(const unsigned char *p) { return p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24); }
static unsigned le16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static long sle32(const unsigned char *p)       /* signed, also where long is 64 bits (the PC harness) */
{
    unsigned long u = le32(p);
    return u & 0x80000000UL ? -(long)((~u + 1) & 0xffffffffUL) : (long)u;
}

static int skip(png_read_fn rd, void *h, unsigned long n)
{
    unsigned char tmp[256];
    while (n) {
        long k = n > sizeof(tmp) ? (long)sizeof(tmp) : (long)n;
        if (!get(rd, h, tmp, k)) return 0;
        n -= (unsigned long)k;
    }
    return 1;
}

/* a BITFIELDS mask -> shift and width, to scale the field to 8 bits */
struct field { int shift, bits; };
static void field(unsigned long m, struct field *f)
{
    f->shift = f->bits = 0;
    if (!m) return;
    while (!(m & 1)) { m >>= 1; f->shift++; }
    while (m & 1) { m >>= 1; f->bits++; }
}
/* field value -> 8 bits as v * 255 / max (what PIL and most viewers do), by table when it fits */
static void scale_table(const struct field *f, unsigned char *t)
{
    unsigned long i, max = f->bits && f->bits <= 8 ? (1UL << f->bits) - 1 : 0;
    for (i = 0; i <= max && max; i++) t[i] = (unsigned char)(i * 255 / max);
}
static unsigned to8(unsigned long v, const struct field *f, const unsigned char *t)
{
    unsigned long x;
    if (!f->bits) return 0;
    x = (v >> f->shift) & (f->bits >= 32 ? 0xffffffffUL : (1UL << f->bits) - 1);
    if (f->bits > 8) return (unsigned)(x >> (f->bits - 8));
    return t[x];
}

/* the DIB: header, palette, pixels.  off = where the pixels start in the file (pos = where the
 * header starts); ico: an icon's image - no file header, the pixels follow the palette, and the
 * height counts the AND mask too (it follows the image and is ignored) */
static int dib(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o,
               unsigned long off, unsigned long pos, int ico)
{
    unsigned char ih[124];
    unsigned long hsize, comp = 0, used = 0;
    unsigned long mask[3] = { 0, 0, 0 };
    struct field fl[3];
    unsigned char st[3][256];
    long w, hgt;
    int bpp, topdown = 0, i, rc = PNG_OK, pe = 4;
    unsigned char *line = 0, *idx = 0;
    long stride, y;

    if (!get(rd, h, ih, 4)) return PNG_BADDATA;
    hsize = le32(ih);
    if (hsize < 12 || hsize > 124 || !get(rd, h, ih + 4, (long)hsize - 4)) return PNG_BADDATA;
    pos += hsize;
    if (ico) off = ~0UL;                        /* no pixel offset: the palette ends where it ends */
    if (hsize == 12) {                          /* OS/2 1.x: 16-bit sizes, 3-byte palette entries */
        w = (long)le16(ih + 4); hgt = (long)le16(ih + 6); bpp = (int)le16(ih + 10); pe = 3;
    } else {
        w = sle32(ih + 4); hgt = sle32(ih + 8); bpp = (int)le16(ih + 14);
        if (hsize >= 20) comp = le32(ih + 16);
        if (hsize >= 36) used = le32(ih + 32);
        if (hsize >= 52) { mask[0] = le32(ih + 40); mask[1] = le32(ih + 44); mask[2] = le32(ih + 48); }
    }
    if (hgt < 0) { hgt = -hgt; topdown = 1; }
    if (ico) hgt /= 2;
    if (w <= 0 || hgt <= 0 || w > 32767 || hgt > 32767) return PNG_UNSUPPORTED;
    if (comp == 3 && hsize == 40) {             /* BITFIELDS masks follow a 40-byte header */
        unsigned char m[12];
        if (!get(rd, h, m, 12)) return PNG_BADDATA;
        mask[0] = le32(m); mask[1] = le32(m + 4); mask[2] = le32(m + 8);
        pos += 12;
    }
    if (comp == 0 && bpp == 16) { mask[0] = 0x7c00; mask[1] = 0x03e0; mask[2] = 0x001f; }
    if (comp == 0 && (bpp == 24 || bpp == 32)) { mask[0] = 0xff0000; mask[1] = 0xff00; mask[2] = 0xff; }
    if (!((bpp == 1 || bpp == 4 || bpp == 8) && comp == 0) && !(bpp == 8 && comp == 1) && !(bpp == 4 && comp == 2) &&
        !((bpp == 16 || bpp == 24 || bpp == 32) && (comp == 0 || comp == 3))) return PNG_UNSUPPORTED;
    if ((comp == 1 || comp == 2) && topdown) return PNG_UNSUPPORTED;
    for (i = 0; i < 3; i++) { field(mask[i], &fl[i]); scale_table(&fl[i], st[i]); }

    o->w = (int)w; o->h = (int)hgt;
    if (bpp <= 8) {
        unsigned char e[4];
        int n = used && used <= (1UL << bpp) ? (int)used : 1 << bpp;
        for (i = 0; i < n; i++) {
            if (pos + pe > off || !get(rd, h, e, pe)) break;  /* a short palette: the rest stays black */
            o->pal[i][0] = e[2]; o->pal[i][1] = e[1]; o->pal[i][2] = e[0];
            pos += pe;
        }
        o->ncolours = 1 << bpp; o->depth = bpp;
    } else {
        o->ncolours = Q_COLOURS; o->depth = 8;
        for (i = 0; i < 256; i++) { o->pal[i][0] = q->pal[i][0]; o->pal[i][1] = q->pal[i][1]; o->pal[i][2] = q->pal[i][2]; }
    }
    if (ico) off = pos;
    if (off < pos || !skip(rd, h, off - pos)) return PNG_BADDATA;
    if (!o->begin(o)) return PNG_ABORT;

    stride = ((w * bpp + 31) / 32) * 4;
    line = al((unsigned long)stride + 4);
    idx = al((unsigned long)w + 8);
    if (!line || !idx) { rc = PNG_NOMEM; goto out; }

    if (comp == 1 || comp == 2) {               /* RLE: bottom-up, escapes for end of line / picture / delta */
        long x = 0;
        int done = 0;
        y = 0;
        while (!done && y < hgt) {
            unsigned char p[2];
            if (!get(rd, h, p, 2)) break;       /* truncated: show what came */
            if (p[0]) {                         /* a run: p[0] pixels of p[1] (RLE4: two alternating) */
                for (i = 0; i < p[0] && x < w; i++, x++)
                    idx[x] = comp == 1 ? p[1] : (unsigned char)(i & 1 ? p[1] & 15 : p[1] >> 4);
                continue;
            }
            if (p[1] == 0 || p[1] == 1) {       /* end of line / of the picture */
                o->row(o, (int)(hgt - 1 - y), idx);
                for (i = 0; i < w; i++) idx[i] = 0;
                x = 0; y++;
                if (p[1] == 1) done = 1;
                continue;
            }
            if (p[1] == 2) {                    /* delta: right dx, up dy */
                unsigned char dd[2];
                if (!get(rd, h, dd, 2)) break;
                if (dd[1]) {
                    int k;
                    o->row(o, (int)(hgt - 1 - y), idx);
                    for (i = 0; i < w; i++) idx[i] = 0;
                    for (k = 1; k < dd[1] && y + k < hgt; k++) o->row(o, (int)(hgt - 1 - y - k), idx);
                    y += dd[1];
                }
                x += dd[0];
                continue;
            }
            {                                   /* absolute: p[1] literal pixels, padded to 16 bits */
                int n = p[1], nbytes = comp == 1 ? n : (n + 1) / 2;
                unsigned char lit[256];
                if (!get(rd, h, lit, nbytes + (nbytes & 1))) break;
                for (i = 0; i < n; i++, x++)
                    if (x < w) idx[x] = comp == 1 ? lit[i] : (unsigned char)(i & 1 ? lit[i >> 1] & 15 : lit[i >> 1] >> 4);
            }
        }
        if (y < hgt && x) o->row(o, (int)(hgt - 1 - y++), idx);   /* a last unterminated line */
        for (i = 0; i < w; i++) idx[i] = 0;
        while (y < hgt) { o->row(o, (int)(hgt - 1 - y), idx); y++; }
        goto out;
    }

    for (y = 0; y < hgt; y++) {
        int row = (int)(topdown ? y : hgt - 1 - y), cy = (row & 3) << 2;
        long x;
        if (!get(rd, h, line, stride)) {        /* truncated: the rest stays black */
            if (!y) { rc = PNG_BADDATA; break; }
            for (x = 0; x < w; x++) idx[x] = 0;
            for (; y < hgt; y++) o->row(o, (int)(topdown ? y : hgt - 1 - y), idx);
            break;
        }
        switch (bpp) {
        case 1:
            if (o->row1) { o->row1(o, row, line); continue; }   /* the row IS the plane */
            for (x = 0; x < w; x++) idx[x] = (unsigned char)((line[x >> 3] >> (7 - (x & 7))) & 1);
            break;
        case 4: for (x = 0; x < w; x++) idx[x] = (unsigned char)(x & 1 ? line[x >> 1] & 15 : line[x >> 1] >> 4); break;
        case 8: o->row(o, row, line); continue;
        case 24: case 32:
            if (comp == 0) {                    /* plain BGR / BGRx: straight table lookups */
                const unsigned char *s = line;
                int step = bpp / 8;
                for (x = 0; x < w; x++, s += step) {
                    int c = cy | (int)(x & 3);
                    idx[x] = (unsigned char)(q->r[c][s[2]] + q->g[c][s[1]] + q->b[c][s[0]]);
                }
                break;
            }
            /* fall through */
        default: {                              /* 16 bits, or masked 24 / 32 */
            const unsigned char *s = line;
            int step = bpp / 8;
            for (x = 0; x < w; x++, s += step) {
                unsigned long v = step == 2 ? le16(s) : step == 3 ? (s[0] | ((unsigned long)s[1] << 8) | ((unsigned long)s[2] << 16)) : le32(s);
                int c = cy | (int)(x & 3);
                idx[x] = (unsigned char)(q->r[c][to8(v, &fl[0], st[0])] + q->g[c][to8(v, &fl[1], st[1])] + q->b[c][to8(v, &fl[2], st[2])]);
            }
        }
        }
        o->row(o, row, idx);
    }
out:
    if (idx) fr(idx);
    if (line) fr(line);
    return rc;
}

int bmp_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    unsigned char fh[14];
    if (!get(rd, h, fh, 14) || fh[0] != 'B' || fh[1] != 'M') return PNG_NOTPNG;
    return dib(rd, h, al, fr, q, o, le32(fh + 10), 14, 0);
}

int bmp_decode_ico(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    return dib(rd, h, al, fr, q, o, 0, 0, 1);
}
