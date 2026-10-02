/* dt_lib.h - what every AmiDT datatype class shares: the library base, the opened libraries,
 * the 8-bit planar bitmap writer and the colour table setup (V40 style, so OS 3.0/3.1 work).
 * A format supplies dt_load() (decode the file into the object) - see dt_png.c.
 */
#ifndef AMIDT_DT_LIB_H
#define AMIDT_DT_LIB_H

#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/classes.h>
#include <graphics/gfx.h>
#include <datatypes/pictureclass.h>

struct DTBase {
    struct Library lib;                 /* 34 bytes */
    struct ExecBase *sysbase;           /* 34 - set by dt_start.S */
    BPTR seglist;                       /* 38 */
    Class *cl;                          /* 42 */
    UWORD pad;                          /* 46 -> 48 */
};

/* a picture being built: the format calls dt_begin once (size + palette), dt_row per row */
struct DTPic {
    Object *o;
    struct BitMap *bm;
    int w, h, depth;
    UBYTE *pbuf;                        /* one row of planar bytes per plane, for the c2p */
};

/* the library bases are defined in dt_lib.c with the NDK's own types (the proto headers declare them) */

int  dt_begin(struct DTPic *p, Object *o, int w, int h, int depth, int ncolours, UBYTE pal[][3]);
void dt_row(struct DTPic *p, int y, const UBYTE *idx);
void dt_c2p_init(void);
void dt_colours(struct DTPic *p, int ncolours, UBYTE pal[][3]);
int  dt_finish(struct DTPic *p, const char *name);
void dt_abort(struct DTPic *p);

/* the file, read through a 16 KB buffer, and a format's decoder run on it into the object.
 * dec() is one of the cores (src/core) bound to its tables; it returns PNG_OK etc. */
struct png_out;
typedef long (*dt_read_fn)(void *h, unsigned char *buf, long n);
typedef int (*dt_dec_fn)(dt_read_fn rd, void *h, struct png_out *o);
long dt_rd(void *h, unsigned char *dst, long n);
void *dt_al(unsigned long n);           /* zeroed */
void dt_fr(void *p);
int  dt_decode_file(Object *o, dt_dec_fn dec, const char *fallback_name);

/* each format */
extern const char dt_libname[], dt_idstring[];
int dt_load(Class *cl, Object *o);      /* 0 = failed (IoErr set) */
int dt_init(void);                      /* once, at the first open (tables) */
void dt_cleanup(void);

#endif
