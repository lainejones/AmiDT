/* pngdec.c - see pngdec.h */
#include "pngdec.h"
#include "inflate.h"

#ifndef PROF
#define PROF 0                          /* profiling builds: 1 = inflate only, 2 = no bitmap writes */
#endif

struct rd {                             /* the file, chunk by chunk */
    png_read_fn read;
    void *h;
    unsigned long left;                 /* bytes left in the current IDAT chunk */
    int done;                           /* past the last IDAT */
};

static int get(struct rd *r, unsigned char *b, long n) { return r->read(r->h, b, n) == n; }
static unsigned long be32(const unsigned char *p) { return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]; }

static int skip(struct rd *r, unsigned long n)
{
    unsigned char tmp[256];
    while (n) {
        long k = n > sizeof(tmp) ? (long)sizeof(tmp) : (long)n;
        if (!get(r, tmp, k)) return 0;
        n -= (unsigned long)k;
    }
    return 1;
}

/* zin's input: the IDAT chunks back to back */
static long idat(void *h, unsigned char *buf, long max)
{
    struct rd *r = h;
    long k;
    while (!r->left) {
        unsigned char hdr[8];
        if (r->done) return 0;
        if (!skip(r, 4) || !get(r, hdr, 8)) { r->done = 1; return 0; }   /* CRC, next header */
        if (hdr[4] != 'I' || hdr[5] != 'D' || hdr[6] != 'A' || hdr[7] != 'T') { r->done = 1; return 0; }
        r->left = be32(hdr);
    }
    k = (unsigned long)max > r->left ? (long)r->left : max;
    k = r->read(r->h, buf, k);
    if (k <= 0) { r->done = 1; return 0; }
    r->left -= (unsigned long)k;
    return k;
}

/* the filters, over n bytes; the left neighbour is bpp bytes back, the first bpp bytes have none.
 * Paeth uses p-a = b-c, p-b = a-c, p-c = (b-c)+(a-c): two subtractions instead of four. */
static int unfilter(int f, unsigned char *cur, const unsigned char *prev, long n, int bpp)
{
    unsigned char *d = cur, *end = cur + n;
    const unsigned char *u = prev;
    long i;
    switch (f) {
    case 0: break;
    case 1: for (d += bpp; d < end; d++) *d = (unsigned char)(*d + d[-bpp]); break;
    case 2: while (d < end) *d++ += *u++; break;
    case 3:
        for (i = 0; i < bpp; i++) d[i] = (unsigned char)(d[i] + (u[i] >> 1));
        for (d += bpp, u += bpp; d < end; d++, u++) *d = (unsigned char)(*d + ((d[-bpp] + *u) >> 1));
        break;
    case 4:
        for (i = 0; i < bpp; i++) d[i] = (unsigned char)(d[i] + u[i]);
        for (d += bpp, u += bpp; d < end; d++, u++) {
            int a = d[-bpp], b = *u, c = u[-bpp], pa = b - c, pb = a - c, pc = pa + pb;
            if (pa < 0) pa = -pa;
            if (pb < 0) pb = -pb;
            if (pc < 0) pc = -pc;
            *d = (unsigned char)(*d + ((pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c)));
        }
        break;
    default: return 0;
    }
    return 1;
}

struct img { int w, h, bd, ct, ch; };

/* one unfiltered row of n pixels -> indices; pixel i sits at x0 + i*dx, row y (for the dither) */
static void to_index(const struct img *m, const struct quant *q, const unsigned char *s, int n,
                     int x0, int dx, int y, unsigned char *out)
{
    int i, x = x0, cy = (y & 3) << 2;
    if (m->ct == 3 || (m->ct == 0 && m->bd < 8)) {           /* packed indices / grey levels */
        int bd = m->bd, per = 8 / bd, mask = (1 << bd) - 1;
        if (bd == 8) { for (i = 0; i < n; i++) out[i] = s[i]; return; }
        for (i = 0; i < n; i++) out[i] = (unsigned char)((s[i / per] >> ((per - 1 - i % per) * bd)) & mask);
        return;
    }
    if (m->ct == 0 || m->ct == 4) {                          /* grey (+alpha): the grey byte */
        int step = m->ch * (m->bd / 8);
        for (i = 0; i < n; i++) out[i] = s[i * step];
        return;
    }
    {                                                        /* RGB / RGBA, 8 or 16 bits */
        int step = m->ch * (m->bd / 8), o = m->bd / 8;      /* 16-bit: the high bytes */
        if (dx == 1 && step == 3 && !(x0 & 3)) {            /* the common case: 8-bit RGB, 4 pixels a turn */
            const unsigned char *r0 = q->r[cy], *g0 = q->g[cy], *b0 = q->b[cy];
            const unsigned char *r1 = r0 + 256, *g1 = g0 + 256, *b1 = b0 + 256;
            const unsigned char *r2 = r1 + 256, *g2 = g1 + 256, *b2 = b1 + 256;
            const unsigned char *r3 = r2 + 256, *g3 = g2 + 256, *b3 = b2 + 256;
            unsigned char *e = out + (n & ~3);
            while (out < e) {
                out[0] = (unsigned char)(r0[s[0]] + g0[s[1]] + b0[s[2]]);
                out[1] = (unsigned char)(r1[s[3]] + g1[s[4]] + b1[s[5]]);
                out[2] = (unsigned char)(r2[s[6]] + g2[s[7]] + b2[s[8]]);
                out[3] = (unsigned char)(r3[s[9]] + g3[s[10]] + b3[s[11]]);
                out += 4; s += 12;
            }
            x = x0 + (n & ~3);                              /* the 0-3 left over go below */
            n &= 3;
        }
        for (i = 0; i < n; i++, x += dx, s += step) {
            int c = cy | (x & 3);
            out[i] = (unsigned char)(q->r[c][s[0]] + q->g[c][s[o]] + q->b[c][s[2 * o]]);
        }
    }
}

int png_decode(png_read_fn rdf, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    static const unsigned char SIG[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    unsigned char b[13];
    struct rd r;
    struct img m;
    struct zin *z = 0;
    unsigned char *cur = 0, *prev = 0, *idx = 0, *full = 0;
    int interlace = 0, i, rc = PNG_OK, bpp, havehdr = 0, npal = 0;
    long rowbytes;

    r.read = rdf; r.h = h; r.left = 0; r.done = 0;
    if (!get(&r, b, 8)) return PNG_NOTPNG;
    for (i = 0; i < 8; i++) if (b[i] != SIG[i]) return PNG_NOTPNG;

    /* the chunks before the image data: IHDR, PLTE, anything else skipped */
    for (;;) {
        unsigned long len;
        if (!get(&r, b, 8)) return PNG_BADDATA;
        len = be32(b);
        if (b[4] == 'I' && b[5] == 'H' && b[6] == 'D' && b[7] == 'R') {
            if (len != 13 || !get(&r, b, 13) || !skip(&r, 4)) return PNG_BADDATA;
            m.w = (int)be32(b); m.h = (int)be32(b + 4); m.bd = b[8]; m.ct = b[9]; interlace = b[12];
            if (b[10] || b[11] || interlace > 1 || m.w <= 0 || m.h <= 0 || m.w > 32767 || m.h > 32767) return PNG_UNSUPPORTED;
            havehdr = 1;
        } else if (b[4] == 'P' && b[5] == 'L' && b[6] == 'T' && b[7] == 'E') {
            unsigned char p[3];
            unsigned long k;
            if (len % 3 || len > 768) return PNG_BADDATA;
            npal = (int)(len / 3);
            for (k = 0; k < (unsigned long)npal; k++) {
                if (!get(&r, p, 3)) return PNG_BADDATA;
                o->pal[k][0] = p[0]; o->pal[k][1] = p[1]; o->pal[k][2] = p[2];
            }
            if (!skip(&r, 4)) return PNG_BADDATA;
        } else if (b[4] == 'I' && b[5] == 'D' && b[6] == 'A' && b[7] == 'T') {
            r.left = len;
            break;
        } else if (b[4] == 'I' && b[5] == 'E' && b[6] == 'N' && b[7] == 'D') {
            return PNG_BADDATA;
        } else if (!skip(&r, len + 4)) return PNG_BADDATA;
    }
    if (!havehdr) return PNG_BADDATA;
    switch (m.ct) {
    case 0: m.ch = 1; if (m.bd != 1 && m.bd != 2 && m.bd != 4 && m.bd != 8 && m.bd != 16) return PNG_UNSUPPORTED; break;
    case 2: m.ch = 3; if (m.bd != 8 && m.bd != 16) return PNG_UNSUPPORTED; break;
    case 3: m.ch = 1; if (m.bd > 8 || !npal) return PNG_UNSUPPORTED; break;
    case 4: m.ch = 2; if (m.bd != 8 && m.bd != 16) return PNG_UNSUPPORTED; break;
    case 6: m.ch = 4; if (m.bd != 8 && m.bd != 16) return PNG_UNSUPPORTED; break;
    default: return PNG_UNSUPPORTED;
    }

    /* the output colours */
    o->w = m.w; o->h = m.h;
    if (m.ct == 3) {
        o->ncolours = npal;
        for (o->depth = 1; (1 << o->depth) < npal; o->depth++) ;
    } else if (m.ct == 0 || m.ct == 4) {
        int levels = m.bd < 8 ? 1 << m.bd : 256;
        o->ncolours = levels;
        o->depth = m.bd < 8 ? m.bd : 8;
        for (i = 0; i < levels; i++) o->pal[i][0] = o->pal[i][1] = o->pal[i][2] = (unsigned char)(i * 255 / (levels - 1));
    } else {
        o->ncolours = Q_COLOURS;
        o->depth = 8;
        for (i = 0; i < 256; i++) { o->pal[i][0] = q->pal[i][0]; o->pal[i][1] = q->pal[i][1]; o->pal[i][2] = q->pal[i][2]; }
    }
    if (!o->begin(o)) return PNG_ABORT;

    bpp = (m.ch * m.bd + 7) / 8;
    rowbytes = ((long)m.w * m.ch * m.bd + 7) / 8;
    z = al(sizeof(struct zin));
    cur = al((unsigned long)rowbytes + 1);
    prev = al((unsigned long)rowbytes + 1);
    idx = al((unsigned long)m.w);
    if (!z || !cur || !prev || !idx) { rc = PNG_NOMEM; goto out; }
    if (!zin_init(z, idat, &r)) { rc = PNG_BADDATA; goto out; }

    if (!interlace) {
        int y, direct = m.bd == 8 && (m.ct == 3 || m.ct == 0) && PROF == 0;   /* the row is already the indices */
        int bits1 = m.bd == 1 && (m.ct == 3 || m.ct == 0) && o->row1 && PROF == 0;   /* ... or the bitplane */
        for (y = 0; y < m.h; y++) {
            unsigned char *t;
            if (zin_read(z, cur, rowbytes + 1) != rowbytes + 1) { rc = PNG_BADDATA; break; }
#if PROF != 1
            if (!unfilter(cur[0], cur + 1, prev + 1, rowbytes, bpp)) { rc = PNG_BADDATA; break; }
            if (direct) { o->row(o, y, cur + 1); t = prev; prev = cur; cur = t; continue; }
            if (bits1) { o->row1(o, y, cur + 1); t = prev; prev = cur; cur = t; continue; }
            to_index(&m, q, cur + 1, m.w, 0, 1, y, idx);
#endif
#if PROF == 0
            o->row(o, y, idx);
#endif
            t = prev; prev = cur; cur = t;
        }
    } else {                                                 /* Adam7: gather, then hand out rows */
        static const unsigned char XS[7] = { 0, 4, 0, 2, 0, 1, 0 }, YS[7] = { 0, 0, 4, 0, 2, 0, 1 };
        static const unsigned char DX[7] = { 8, 8, 4, 4, 2, 2, 1 }, DY[7] = { 8, 8, 8, 4, 4, 2, 2 };
        int p, y;
        if (!(full = al((unsigned long)m.w * (unsigned long)m.h))) { rc = PNG_NOMEM; goto out; }
        for (p = 0; p < 7 && rc == PNG_OK; p++) {
            int pw = (m.w - XS[p] + DX[p] - 1) / DX[p], ph = (m.h - YS[p] + DY[p] - 1) / DY[p], py;
            long rb;
            if (pw <= 0 || ph <= 0) continue;
            rb = ((long)pw * m.ch * m.bd + 7) / 8;
            for (i = 0; i <= rb; i++) prev[i] = 0;
            for (py = 0; py < ph; py++) {
                unsigned char *t;
                int k, yy = YS[p] + py * DY[p];
                if (zin_read(z, cur, rb + 1) != rb + 1 || !unfilter(cur[0], cur + 1, prev + 1, rb, bpp)) { rc = PNG_BADDATA; break; }
                to_index(&m, q, cur + 1, pw, XS[p], DX[p], yy, idx);
                for (k = 0; k < pw; k++) full[(long)yy * m.w + XS[p] + k * DX[p]] = idx[k];
                t = prev; prev = cur; cur = t;
            }
        }
        if (rc == PNG_OK) for (y = 0; y < m.h; y++) o->row(o, y, full + (long)y * m.w);
    }
out:
    if (full) fr(full);
    if (idx) fr(idx);
    if (prev) fr(prev);
    if (cur) fr(cur);
    if (z) fr(z);
    return rc;
}
