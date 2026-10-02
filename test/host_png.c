/* host_png.c - the AmiDT cores on the PC (PNG, GIF, BMP - picked by the file's magic): decode and write what the datatype would make
 * (size, palette, 8-bit indices) for test/check_png.py to compare against PIL.
 *
 *   host_png in.png out.idx      -> "AIDX" w h ncolours depth, pal[256][3], indices[w*h]; rc 0
 *   host_png in.png -            -> just the result code (corrupt files must fail, not crash)
 *   host_png -time in.png N      -> decode N times, print ms per decode
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/core/pngdec.h"
#include "../src/core/gifdec.h"
#include "../src/core/bmpdec.h"
#include "../src/core/moredec.h"

static long rd(void *h, unsigned char *b, long n) { return (long)fread(b, 1, (size_t)n, (FILE *)h); }
/* like an Amiga: a huge picture (a corrupt header) just fails to get its memory */
#define MAXALLOC (64UL << 20)
static void *al(unsigned long n) { return n > MAXALLOC ? NULL : calloc(1, n ? n : 1); }
static void fr(void *p) { free(p); }

static unsigned char *img;
static int begin(struct png_out *o) { img = al((unsigned long)o->w * o->h); return img != NULL; }
static void row(struct png_out *o, int y, const unsigned char *idx) { memcpy(img + (size_t)y * o->w, idx, (size_t)o->w); }
static void row1(struct png_out *o, int y, const unsigned char *bits)
{
    int x;
    for (x = 0; x < o->w; x++) img[(size_t)y * o->w + x] = (bits[x >> 3] >> (7 - (x & 7))) & 1;
}

static int decode(const char *path, struct quant *q, struct png_out *o)
{
    FILE *f = fopen(path, "rb");
    unsigned char m[4] = { 0, 0, 0, 0 };
    size_t n;
    int rc;
    if (!f) return -1;
    memset(o, 0, sizeof(*o));
    o->begin = begin; o->row = row; o->row1 = row1;
    if (fread(m, 1, 4, f) != 4) m[0] = 0;
    rewind(f);
    n = strlen(path);
    if (n > 4 && (!strcmp(path + n - 4, ".tga") || !strcmp(path + n - 4, ".TGA"))) rc = tga_decode(rd, f, al, fr, q, o);
    else if (m[0] == 'G' && m[1] == 'I') rc = gif_decode(rd, f, al, fr, o);         /* by the file's own magic */
    else if (m[0] == 'B' && m[1] == 'M') rc = bmp_decode(rd, f, al, fr, q, o);
    else if (m[0] == 0x0A) rc = pcx_decode(rd, f, al, fr, q, o);
    else if (!m[0] && !m[1] && m[2] == 1 && !m[3]) rc = ico_decode(rd, f, al, fr, q, o);
    else if (m[0] == 'q' && m[1] == 'o' && m[2] == 'i' && m[3] == 'f') rc = qoi_decode(rd, f, al, fr, q, o);
    else rc = png_decode(rd, f, al, fr, q, o);
    fclose(f);
    return rc;
}

int main(int argc, char **argv)
{
    static struct quant q;
    struct png_out o;
    int rc;
    q_init(&q);
    if (argc == 4 && !strcmp(argv[1], "-time")) {
        int n = atoi(argv[3]), i;
        clock_t t0 = clock();
        for (i = 0; i < n; i++) { decode(argv[2], &q, &o); free(img); img = NULL; }
        printf("%.2f ms per decode\n", (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC / n);
        return 0;
    }
    if (argc != 3) { fprintf(stderr, "usage: host_png in.png out.idx|-\n"); return 2; }
    rc = decode(argv[1], &q, &o);
    if (rc == PNG_OK && strcmp(argv[2], "-")) {
        FILE *f = fopen(argv[2], "wb");
        int hdr[4] = { o.w, o.h, o.ncolours, o.depth };
        fwrite("AIDX", 1, 4, f); fwrite(hdr, sizeof(int), 4, f);
        fwrite(o.pal, 1, 768, f); fwrite(img, 1, (size_t)o.w * o.h, f);
        fclose(f);
    }
    free(img);
    printf("%d\n", rc);
    return rc;
}
