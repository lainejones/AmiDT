/* tgadec.c - Truevision TGA, see moredec.h */
#include "moredec.h"

static int get(png_read_fn rd, void *h, unsigned char *b, long n) { return rd(h, b, n) == n; }

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

/* 5 bits -> 8, as v * 255 / 31 (the same widening as bmpdec) */
static unsigned char w5(unsigned v) { return (unsigned char)(v * 255 / 31); }

struct rle { int on, left, raw; unsigned char px[4]; };

/* n pixels of bpb bytes each into dst, through the RLE packets (which may cross rows) */
static int pixels(png_read_fn rd, void *h, struct rle *r, unsigned char *dst, int n, int bpb)
{
    if (!r->on) return get(rd, h, dst, (long)n * bpb);
    while (n > 0) {
        int k, i;
        if (!r->left) {
            unsigned char c;
            if (!get(rd, h, &c, 1)) return 0;
            r->left = (c & 127) + 1;
            r->raw = !(c & 128);
            if (!r->raw && !get(rd, h, r->px, bpb)) return 0;
        }
        k = r->left < n ? r->left : n;
        if (r->raw) { if (!get(rd, h, dst, (long)k * bpb)) return 0; dst += k * bpb; }
        else for (i = 0; i < k; i++) { int b; for (b = 0; b < bpb; b++) *dst++ = r->px[b]; }
        r->left -= k; n -= k;
    }
    return 1;
}

int tga_decode(png_read_fn rd, void *h, png_alloc_fn al, png_free_fn fr, const struct quant *q, struct png_out *o)
{
    unsigned char hd[18], *line = 0, *idx = 0, *rgb = 0;
    int type, cmtype, cmfirst, cmlen, cmbits, w, ht, bpp, bpb, topdown, rtl, i, rc = PNG_OK, y, x;
    struct rle r;
    if (!get(rd, h, hd, 18)) return PNG_NOTPNG;
    cmtype = hd[1]; type = hd[2];
    cmfirst = hd[3] | (hd[4] << 8); cmlen = hd[5] | (hd[6] << 8); cmbits = hd[7];
    w = hd[12] | (hd[13] << 8); ht = hd[14] | (hd[15] << 8); bpp = hd[16];
    topdown = (hd[17] & 0x20) != 0; rtl = (hd[17] & 0x10) != 0;
    if (cmtype > 1 || !((type >= 1 && type <= 3) || (type >= 9 && type <= 11))) return PNG_NOTPNG;
    if (!w || !ht) return PNG_BADDATA;
    r.on = type >= 9; r.left = 0; r.raw = 0;
    type &= 3;
    if (type == 1 && (bpp != 8 || !cmtype || cmfirst + cmlen > 256 || !cmlen)) return PNG_UNSUPPORTED;
    if (type == 2 && bpp != 15 && bpp != 16 && bpp != 24 && bpp != 32) return PNG_UNSUPPORTED;
    if (type == 3 && bpp != 8 && bpp != 16) return PNG_UNSUPPORTED;
    if (cmtype && (cmbits != 15 && cmbits != 16 && cmbits != 24 && cmbits != 32)) return PNG_UNSUPPORTED;
    if (!skip(rd, h, hd[0])) return PNG_BADDATA;                    /* the image ID */
    o->w = w; o->h = ht;
    if (cmtype) {                               /* the colour map (also skipped when not used) */
        int eb = (cmbits + 7) / 8;
        for (i = 0; i < cmlen; i++) {
            unsigned char e[4];
            if (!get(rd, h, e, eb)) return PNG_BADDATA;
            if (type != 1 || cmfirst + i > 255) continue;
            if (eb == 2) {
                unsigned v = e[0] | (e[1] << 8);
                o->pal[cmfirst + i][0] = w5((v >> 10) & 31); o->pal[cmfirst + i][1] = w5((v >> 5) & 31); o->pal[cmfirst + i][2] = w5(v & 31);
            } else { o->pal[cmfirst + i][0] = e[2]; o->pal[cmfirst + i][1] = e[1]; o->pal[cmfirst + i][2] = e[0]; }
        }
    }
    if (type == 1) { o->ncolours = 256; o->depth = 8; }
    else if (type == 3) {
        o->ncolours = 256; o->depth = 8;
        for (i = 0; i < 256; i++) o->pal[i][0] = o->pal[i][1] = o->pal[i][2] = (unsigned char)i;
    } else {
        o->ncolours = Q_COLOURS; o->depth = 8;
        for (i = 0; i < 256; i++) { o->pal[i][0] = q->pal[i][0]; o->pal[i][1] = q->pal[i][1]; o->pal[i][2] = q->pal[i][2]; }
    }
    if (type == 1) {                            /* the fewest planes the used colours need */
        int top = cmfirst + cmlen;
        for (o->depth = 1; (1 << o->depth) < top; o->depth++) ;
        o->ncolours = 1 << o->depth;
    }
    if (!o->begin(o)) return PNG_ABORT;
    bpb = (bpp + 7) / 8;
    line = al((unsigned long)w * bpb);
    idx = al((unsigned long)w);
    if (bpb == 2 && type == 2) rgb = al((unsigned long)w * 3);
    if (!line || !idx || (bpb == 2 && type == 2 && !rgb)) { rc = PNG_NOMEM; goto out; }
    for (y = 0; y < ht; y++) {
        int row = topdown ? y : ht - 1 - y;
        if (!pixels(rd, h, &r, line, w, bpb)) {         /* truncated: the rest stays black */
            if (!y) { rc = PNG_BADDATA; break; }
            for (x = 0; x < w; x++) idx[x] = 0;
            for (; y < ht; y++) o->row(o, topdown ? y : ht - 1 - y, idx);
            break;
        }
        if (rtl) {                              /* right to left: mirror the row */
            for (x = 0; x < w / 2; x++) {
                int b;
                for (b = 0; b < bpb; b++) {
                    unsigned char t = line[x * bpb + b];
                    line[x * bpb + b] = line[(w - 1 - x) * bpb + b];
                    line[(w - 1 - x) * bpb + b] = t;
                }
            }
        }
        if (type == 1) { o->row(o, row, line); continue; }
        if (type == 3) {
            if (bpb == 1) { o->row(o, row, line); continue; }
            for (x = 0; x < w; x++) idx[x] = line[x * 2];
        } else if (bpb == 2) {
            for (x = 0; x < w; x++) {
                unsigned v = line[x * 2] | (line[x * 2 + 1] << 8);
                rgb[x * 3] = w5((v >> 10) & 31); rgb[x * 3 + 1] = w5((v >> 5) & 31); rgb[x * 3 + 2] = w5(v & 31);
            }
            q_row(q, rgb, 3, w, row, idx);
        } else q_row_bgr(q, line, bpb, w, row, idx);
        o->row(o, row, idx);
    }
out:
    if (rgb) fr(rgb);
    if (idx) fr(idx);
    if (line) fr(line);
    return rc;
}
