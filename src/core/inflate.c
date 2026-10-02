/* inflate.c - see inflate.h */
#include "inflate.h"

/* 4-byte moves that may be unaligned: fine on a 68020+ and on the PC; the attribute keeps gcc
 * from assuming they don't alias the byte stores around them */
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__) || defined(__i386__) || defined(__x86_64__)
#define UNALIGNED_OK 1                          /* a 68000 build must not: odd-address word/long = address error */
#endif
typedef unsigned int __attribute__((may_alias)) ul_a;     /* 32 bits on both */

static const unsigned short LBASE[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const unsigned char  LEXTRA[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const unsigned short DBASE[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const unsigned char  DEXTRA[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
static const unsigned char  CLORDER[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };

static int refill(struct zin *z)
{
    if (z->inpos < z->inlen) return 1;
    z->inlen = z->src(z->h, z->in, sizeof(z->in));
    z->inpos = 0;
    if (z->inlen < 0) z->inlen = 0;
    return z->inlen > 0;
}

/* make sure at least n (<= 25) bits are in the buffer; past the end of input pad with zeros */
static void need(struct zin *z, int n)
{
    while (z->nbits < n) {
        unsigned long b = 0;
        if (z->inpos < z->inlen || refill(z)) b = z->in[z->inpos++];
        z->bits |= b << z->nbits;
        z->nbits += 8;
    }
}

static unsigned long getbits(struct zin *z, int n)
{
    unsigned long v;
    if (!n) return 0;
    need(z, n);
    v = z->bits & ((1UL << n) - 1);
    z->bits >>= n;
    z->nbits -= n;
    return v;
}

/* canonical Huffman table from code lengths; 0 = an over-subscribed (bad) set */
static int build(struct zin_huff *t, const unsigned char *len, int n)
{
    unsigned short offs[16];
    int i, left = 1;
    for (i = 0; i < 16; i++) t->count[i] = 0;
    for (i = 0; i < n; i++) t->count[len[i]]++;
    t->count[0] = 0;
    for (i = 1; i < 16; i++) { left <<= 1; left -= t->count[i]; if (left < 0) return 0; }
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = offs[i] + t->count[i];
    for (i = 0; i < n; i++) if (len[i]) t->sym[offs[len[i]]++] = (unsigned short)i;
    /* the fast table: every code up to ZIN_FAST bits, stored bit-reversed (deflate sends codes MSB first) */
    for (i = 0; i < (1 << ZIN_FAST); i++) t->fast[i] = 0;
    {
        int code = 0, k = 0, l;
        for (l = 1; l <= ZIN_FAST; l++) {
            int c;
            for (c = 0; c < t->count[l]; c++, k++, code++) {
                int r = 0, b, fill;
                for (b = 0; b < l; b++) r |= ((code >> b) & 1) << (l - 1 - b);
                for (fill = r; fill < (1 << ZIN_FAST); fill += 1 << l)
                    t->fast[fill] = (unsigned short)((t->sym[k] << 4) | l);
            }
            code <<= 1;
        }
    }
    return 1;
}

static int decode(struct zin *z, const struct zin_huff *t)
{
    int code = 0, first = 0, index = 0, l;
    unsigned short e;
    need(z, 16);
    e = t->fast[z->bits & ((1 << ZIN_FAST) - 1)];
    if (e) {
        l = e & 15;
        z->bits >>= l; z->nbits -= l;
        return e >> 4;
    }
    for (l = 1; l < 16; l++) {                  /* the slow path: one bit at a time */
        int count;
        code |= (int)(z->bits & 1);
        z->bits >>= 1; z->nbits--;
        count = t->count[l];
        if (code - count < first) return t->sym[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int fixed_tables(struct zin *z)
{
    unsigned char len[320];
    int i;
    for (i = 0; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < 288; i++) len[i] = 8;
    if (!build(&z->lit, len, 288)) return 0;
    for (i = 0; i < 30; i++) len[i] = 5;
    return build(&z->dist, len, 30);
}

static int dynamic_tables(struct zin *z)
{
    unsigned char len[320], cl[19];
    int nlen = (int)getbits(z, 5) + 257, ndist = (int)getbits(z, 5) + 1, ncode = (int)getbits(z, 4) + 4, i, n = 0;
    if (nlen > 286 || ndist > 30) return 0;
    for (i = 0; i < 19; i++) cl[i] = 0;
    for (i = 0; i < ncode; i++) cl[CLORDER[i]] = (unsigned char)getbits(z, 3);
    if (!build(&z->lit, cl, 19)) return 0;
    while (n < nlen + ndist) {
        int sym = decode(z, &z->lit), rep = 0, val = 0;
        if (sym < 0) return 0;
        if (sym < 16) { len[n++] = (unsigned char)sym; continue; }
        if (sym == 16) { if (!n) return 0; val = len[n - 1]; rep = 3 + (int)getbits(z, 2); }
        else if (sym == 17) rep = 3 + (int)getbits(z, 3);
        else rep = 11 + (int)getbits(z, 7);
        if (n + rep > nlen + ndist) return 0;
        while (rep--) len[n++] = (unsigned char)val;
    }
    if (!len[256]) return 0;
    return build(&z->lit, len, nlen) && build(&z->dist, len + nlen, ndist);
}


int zin_init(struct zin *z, zin_src src, void *h)
{
    unsigned long cmf, flg;
    z->src = src; z->h = h;
    z->inpos = z->inlen = 0;
    z->bits = 0; z->nbits = 0;
    z->state = 0; z->final = 0; z->stored = 0;
    z->wp = z->rp = 0;
    cmf = getbits(z, 8); flg = getbits(z, 8);
    if ((cmf & 15) != 8 || ((cmf << 8) | flg) % 31 || (flg & 32)) { z->state = -1; return 0; }
    return 1;
}

/* the bit buffer lives in locals inside the Huffman loop.  NEED(n) (n <= 15) takes 16 bits
 * at once while there are 2 input bytes in hand, which is nearly always; the rest goes out of line. */
static __attribute__((noinline)) void more(struct zin *z, int n)
{
    if (z->inpos >= z->inlen) refill(z);
    need(z, n);
}
#define NEED(n) if (nb < (n)) {                                                     \
        if (ie - ip >= 2) { bits |= ((unsigned long)ip[0] | ((unsigned long)ip[1] << 8)) << nb; ip += 2; nb += 16; } \
        else { z->bits = bits; z->nbits = nb; z->inpos = ip - z->in; more(z, (n));   \
               bits = z->bits; nb = z->nbits; ip = z->in + z->inpos; ie = z->in + z->inlen; } \
    }
#define DROP(n) (bits >>= (n), nb -= (n))

/* a code longer than the fast table: canonical decode a bit at a time (needs 15 bits in hand).
 * Returns (length << 16) | symbol, or -1. */
static __attribute__((noinline)) long slowdec(const struct zin_huff *t, unsigned long bits)
{
    int code = 0, first = 0, index = 0, l;
    for (l = 1; l < 16; l++) {
        int count = t->count[l];
        code |= (int)(bits & 1);
        bits >>= 1;
        if (code - count < first) return ((long)l << 16) | t->sym[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* one Huffman block's symbols into buf from z->wp, until the block ends (state 0), lim is
 * reached, or the data is bad (state -1) */
static __attribute__((noinline)) void huff(struct zin *z, unsigned char *lim)
{
    unsigned char *out = z->buf + z->wp;
    unsigned long bits = z->bits;
    int nb = z->nbits;
    const unsigned char *ip = z->in + z->inpos, *ie = z->in + z->inlen;
    const unsigned short *lfast = z->lit.fast;
    while (out < lim) {
        int sym;
        unsigned short e;
        NEED(15);
        e = lfast[bits & ((1 << ZIN_FAST) - 1)];
        if (e) { DROP(e & 15); sym = e >> 4; }
        else { long r = slowdec(&z->lit, bits); if (r < 0) goto bad; DROP((int)(r >> 16)); sym = (int)(r & 0xffff); }
        if (sym < 256) { *out++ = (unsigned char)sym; continue; }
        if (sym == 256) { z->state = 0; break; }
        sym -= 257;
        if (sym >= 29) goto bad;
        {
            int len = LBASE[sym], d, x;
            unsigned long dist;
            const unsigned char *s;
            if ((x = LEXTRA[sym]) != 0) { NEED(x); len += (int)(bits & ((1UL << x) - 1)); DROP(x); }
            NEED(15);
            e = z->dist.fast[bits & ((1 << ZIN_FAST) - 1)];
            if (e) { DROP(e & 15); d = e >> 4; }
            else { long r = slowdec(&z->dist, bits); if (r < 0) goto bad; DROP((int)(r >> 16)); d = (int)(r & 0xffff); }
            if (d < 0 || d >= 30) goto bad;
            dist = DBASE[d];
            if ((x = DEXTRA[d]) != 0) { NEED(x); dist += bits & ((1UL << x) - 1); DROP(x); }
            if (dist > (unsigned long)(out - z->buf)) goto bad;
            s = out - dist;
#ifdef UNALIGNED_OK
            if (dist >= 4) {                        /* 4 at a time; may write up to 3 past the end (the slack) */
                do { *(ul_a *)out = *(const ul_a *)s; out += 4; s += 4; } while ((len -= 4) > 0);
                out += len;                         /* len is now -3..0: back to the true end */
            } else
#endif
                do *out++ = *s++; while (--len);
        }
    }
    if (0) bad: z->state = -1;
    z->bits = bits; z->nbits = nb; z->inpos = ip - z->in;
    z->wp = out - z->buf;
}

/* decode one or more blocks' worth into buf until it holds ZIN_BUF bytes (or the data ends).
 * Called only when everything written has been handed out (rp == wp). */
static void produce(struct zin *z)
{
    unsigned char *buf = z->buf, *out, *lim;
    if (z->wp > ZIN_BUF - 4096) {                   /* keep the last 32 KB, slide it to the front */
        unsigned char *d = buf, *s = buf + z->wp - ZIN_WSIZE;
        long k = ZIN_WSIZE;
        do *d++ = *s++; while (--k);                /* d < s: an overlap copies correctly upward */
        z->wp = z->rp = ZIN_WSIZE;
    }
    out = buf + z->wp;
    lim = buf + ZIN_BUF;
    while (out < lim) {
        if (z->state == 0) {                        /* a new block */
            int type;
            z->wp = out - buf;
            if (z->final) { z->state = 3; return; }
            z->final = (int)getbits(z, 1);
            type = (int)getbits(z, 2);
            if (type == 0) {
                unsigned long len, nlen;
                z->bits >>= z->nbits & 7; z->nbits -= z->nbits & 7;     /* to a byte boundary */
                len = getbits(z, 16); nlen = getbits(z, 16);
                if ((len ^ 0xffff) != nlen) { z->state = -1; return; }
                z->stored = (long)len;
                z->state = 1;
            } else if (type == 1) { if (!fixed_tables(z)) { z->state = -1; return; } z->state = 2; }
            else if (type == 2) { if (!dynamic_tables(z)) { z->state = -1; return; } z->state = 2; }
            else { z->state = -1; return; }
        }
        if (z->state == 1) {                        /* stored: what's left in the bit buffer, then straight from the input */
            while (z->stored && out < lim) {
                if (z->nbits >= 8) { *out++ = (unsigned char)z->bits; z->bits >>= 8; z->nbits -= 8; z->stored--; continue; }
                if (!(z->inpos < z->inlen || refill(z))) { z->wp = out - buf; z->state = -1; return; }
                {
                    long k = z->inlen - z->inpos;
                    const unsigned char *s = z->in + z->inpos;
                    if (k > z->stored) k = z->stored;
                    if (k > lim - out) k = lim - out;
                    z->inpos += k; z->stored -= k;
                    while (k--) *out++ = *s++;
                }
            }
            if (!z->stored) z->state = 0;
            continue;
        }
        if (z->state == 2) {                        /* huffman */
            z->wp = out - buf;
            huff(z, lim);
            out = buf + z->wp;
            if (z->state < 0) return;
            continue;
        }
        break;                                      /* done or error */
    }
    z->wp = out - buf;
}

long zin_read(struct zin *z, unsigned char *dst, long n)
{
    long got = 0;
    while (got < n) {
        long k = z->wp - z->rp;
        const unsigned char *s;
        if (k <= 0) {
            if (z->state == 3 || z->state < 0) break;
            produce(z);
            continue;
        }
        if (k > n - got) k = n - got;
        s = z->buf + z->rp;
        z->rp += k; got += k;
#ifdef UNALIGNED_OK
        while (k >= 16) {
            ((ul_a *)dst)[0] = ((const ul_a *)s)[0]; ((ul_a *)dst)[1] = ((const ul_a *)s)[1];
            ((ul_a *)dst)[2] = ((const ul_a *)s)[2]; ((ul_a *)dst)[3] = ((const ul_a *)s)[3];
            dst += 16; s += 16; k -= 16;
        }
#endif
        while (k--) *dst++ = *s++;
    }
    return got;
}
