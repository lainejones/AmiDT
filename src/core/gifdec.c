/* gifdec.c - see gifdec.h */
#include "gifdec.h"

#define MAXCODES 4096

struct lzw {
    unsigned short prefix[MAXCODES];
    unsigned char  suffix[MAXCODES];
    unsigned char  first[MAXCODES];             /* each string's first byte */
    unsigned short len[MAXCODES];               /* and its length */
    unsigned char  stack[MAXCODES + 1];
    unsigned char  blk[256];                    /* the current data sub-block */
    int blen, bpos, ended;                      /* ended: the 0-length block was read */
};

static int get(png_read_fn rd, void *h, unsigned char *b, long n) { return rd(h, b, n) == n; }

/* skip data sub-blocks up to and including the terminator */
static int skip_blocks(png_read_fn rd, void *h)
{
    unsigned char n, tmp[255];
    for (;;) {
        if (!get(rd, h, &n, 1)) return 0;
        if (!n) return 1;
        if (!get(rd, h, tmp, n)) return 0;
    }
}

struct dst {                                    /* where the image's pixels go */
    struct png_out *o;
    unsigned char *row;                         /* one screen row (streaming) */
    unsigned char *full;                        /* or the whole screen (interlaced / partial) */
    int sw, left, top, fw, fh, interlace;
    int x, y, pass;                             /* place within the image */
    int done;
};

static const unsigned char ISTART[4] = { 0, 4, 2, 1 }, ISTEP[4] = { 8, 8, 4, 2 };

/* where the image's pixel x of the current row goes */
static unsigned char *rowptr(struct dst *d)
{
    return d->full ? d->full + (long)(d->top + d->y) * d->sw + d->left + d->x : d->row + d->left + d->x;
}

/* the current row of the image is complete */
static void row_done(struct dst *d)
{
    d->x = 0;
    if (!d->full) d->o->row(d->o, d->y, d->row);
    if (d->interlace) {
        d->y += ISTEP[d->pass];
        while (d->y >= d->fh && d->pass < 3) { d->pass++; d->y = ISTART[d->pass]; }
        if (d->y >= d->fh) d->done = 1;
    } else if (++d->y >= d->fh) d->done = 1;
}

/* n pixels of the image, in order (a string that crosses the end of a row) */
static void put(struct dst *d, const unsigned char *p, int n)
{
    while (n > 0 && !d->done) {
        int k = d->fw - d->x, i;
        unsigned char *t = rowptr(d);
        if (k > n) k = n;
        for (i = 0; i < k; i++) t[i] = p[i];
        p += k; n -= k; d->x += k;
        if (d->x == d->fw) row_done(d);
    }
}

/* the LZW data: sub-blocks, LSB-first codes of growing size */
static int lzw(png_read_fn rd, void *h, struct lzw *z, int minsize, struct dst *d)
{
    unsigned long bits = 0;
    int nb = 0, size = minsize + 1, clear = 1 << minsize, next = clear + 2, old = -1, code0;
    unsigned char *sp;
    z->blen = z->bpos = z->ended = 0;
    for (code0 = 0; code0 < clear; code0++) z->first[code0] = (unsigned char)code0, z->len[code0] = 1;
    while (!d->done) {
        int code;
        while (nb < size) {                     /* more bits */
            if (z->bpos >= z->blen) {
                unsigned char n;
                if (z->ended || !get(rd, h, &n, 1)) return d->done ? 1 : 0;
                if (!n) { z->ended = 1; return d->done; }
                if (!get(rd, h, z->blk, n)) return 0;
                z->blen = n; z->bpos = 0;
            }
            bits |= (unsigned long)z->blk[z->bpos++] << nb;
            nb += 8;
        }
        code = (int)(bits & ((1UL << size) - 1));
        bits >>= size; nb -= size;
        if (code == clear) { size = minsize + 1; next = clear + 2; old = -1; continue; }
        if (code == clear + 1) break;           /* end of information */
        if (old < 0) {                          /* the first code after a clear: a plain byte */
            unsigned char c;
            if (code >= clear) return 0;
            c = (unsigned char)code;
            put(d, &c, 1);
            old = code;
            continue;
        }
        if (code > next || (code == next && next >= MAXCODES)) return 0;
        {
            int c = code, l = code == next ? z->len[old] + 1 : z->len[code];
            unsigned char *t;
            if (d->fw - d->x >= l) {            /* fits the row: written in place, backwards */
                t = rowptr(d) + l;
                if (code == next) { *--t = z->first[old]; c = old; }   /* KwKwK */
                while (c >= clear) { *--t = z->suffix[c]; c = z->prefix[c]; }
                *--t = (unsigned char)c;
                d->x += l;
                if (d->x == d->fw) row_done(d);
            } else {                            /* crosses a row end: build it, then hand it out */
                sp = z->stack + MAXCODES;
                if (code == next) { *--sp = z->first[old]; c = old; }
                while (c >= clear) { *--sp = z->suffix[c]; c = z->prefix[c]; }
                *--sp = (unsigned char)c;
                put(d, sp, l);
            }
            if (next < MAXCODES) {
                z->prefix[next] = (unsigned short)old;
                z->suffix[next] = z->first[code == next ? old : code];
                z->first[next] = z->first[old];
                z->len[next] = (unsigned short)(z->len[old] + 1);
                next++;
                if (next == (1 << size) && size < 12) size++;
            }
        }
        old = code;
    }
    return 1;
}

int gif_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, struct png_out *o)
{
    unsigned char b[13], ib[10];
    struct lzw *z = 0;
    struct dst d;
    int i, sw, sh, gct = 0, bg, rc = PNG_OK, ncol = 0, gbits = 0;
    if (!get(rd, h, b, 13)) return PNG_NOTPNG;
    if (b[0] != 'G' || b[1] != 'I' || b[2] != 'F' || b[3] != '8' || (b[4] != '7' && b[4] != '9') || b[5] != 'a') return PNG_NOTPNG;
    sw = b[6] | (b[7] << 8); sh = b[8] | (b[9] << 8); bg = b[11];
    if (b[10] & 0x80) {
        gbits = (b[10] & 7) + 1; gct = 1 << gbits;
        for (i = 0; i < gct; i++) { if (!get(rd, h, o->pal[i], 3)) return PNG_BADDATA; }
    }
    for (;;) {                                  /* to the first image */
        unsigned char t;
        if (!get(rd, h, &t, 1)) return PNG_BADDATA;
        if (t == 0x2C) break;
        if (t == 0x3B) return PNG_BADDATA;      /* trailer, no image */
        if (t != 0x21) return PNG_BADDATA;
        if (!get(rd, h, &t, 1) || !skip_blocks(rd, h)) return PNG_BADDATA;   /* any extension */
    }
    if (!get(rd, h, ib, 9)) return PNG_BADDATA;
    d.left = ib[0] | (ib[1] << 8); d.top = ib[2] | (ib[3] << 8);
    d.fw = ib[4] | (ib[5] << 8); d.fh = ib[6] | (ib[7] << 8);
    d.interlace = (ib[8] & 0x40) != 0;
    if (!sw || !sh) { sw = d.left + d.fw; sh = d.top + d.fh; }  /* some writers leave the screen 0 */
    if (d.fw <= 0 || d.fh <= 0 || sw <= 0 || sh <= 0) return PNG_BADDATA;
    if (d.left + d.fw > sw || d.top + d.fh > sh) {              /* off the screen: clip to it */
        if (d.left >= sw || d.top >= sh) return PNG_BADDATA;
        if (d.left + d.fw > sw) sw = d.left + d.fw;              /* (grow the screen instead) */
        if (d.top + d.fh > sh) sh = d.top + d.fh;
    }
    if (ib[8] & 0x80) {                         /* a local colour table replaces the global one */
        gbits = (ib[8] & 7) + 1; ncol = 1 << gbits;
        for (i = 0; i < ncol; i++) { if (!get(rd, h, o->pal[i], 3)) return PNG_BADDATA; }
    } else if (gct) ncol = gct;
    else {                                      /* no colours at all: grey */
        gbits = 8; ncol = 256;
        for (i = 0; i < 256; i++) o->pal[i][0] = o->pal[i][1] = o->pal[i][2] = (unsigned char)i;
    }
    o->w = sw; o->h = sh; o->ncolours = ncol; o->depth = gbits;
    if (bg >= ncol) bg = 0;
    {
        unsigned char m;
        if (!get(rd, h, &m, 1) || m < 1 || m > 11) return PNG_BADDATA;
        if (!o->begin(o)) return PNG_ABORT;
        z = al(sizeof(*z));
        d.o = o; d.sw = sw; d.x = d.y = d.pass = d.done = 0; d.row = d.full = 0;
        if (!z) { rc = PNG_NOMEM; goto out; }
        if (d.interlace || d.fw != sw || d.fh != sh) {
            if (!(d.full = al((unsigned long)sw * (unsigned long)sh))) { rc = PNG_NOMEM; goto out; }
            if (bg) for (i = 0; i < sw * sh; i++) d.full[i] = (unsigned char)bg;
        } else if (!(d.row = al((unsigned long)sw))) { rc = PNG_NOMEM; goto out; }
        if (!lzw(rd, h, z, m, &d) && !d.done) {
            /* truncated data: show what arrived (rows not reached stay background), like browsers */
            if (!d.y && !d.pass && !d.x) { rc = PNG_BADDATA; goto out; }
            if (!d.full) { for (i = 0; i < sw; i++) d.row[i] = (unsigned char)bg;
                           if (d.x) o->row(o, d.y++, d.row);
                           while (d.y < sh) o->row(o, d.y++, d.row); }
        }
        if (d.full) for (i = 0; i < sh; i++) o->row(o, i, d.full + (long)i * sw);
    }
out:
    if (d.full) fr(d.full);
    if (d.row) fr(d.row);
    if (z) fr(z);
    return rc;
}
