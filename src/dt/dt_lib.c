/* dt_lib.c - the library and class side every AmiDT datatype shares (see dt_lib.h).
 *
 * The class is PRIVATE (MakeClass with no ID): datatypes.library only ever reaches it through
 * ObtainEngine, so it never meets the system class of the same format in the public list.
 */
#include <exec/execbase.h>
#include <dos/dosextens.h>
#include <graphics/gfxbase.h>
#include <intuition/intuitionbase.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/modeid.h>
#include <intuition/classusr.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/datatypes.h>
#include <clib/alib_protos.h>
#include "dt_lib.h"
#include "../core/pngdec.h"

struct ExecBase *SysBase;
struct DosLibrary *DOSBase;
struct GfxBase *GfxBase;
struct IntuitionBase *IntuitionBase;
struct Library *UtilityBase, *DataTypesBase;
static struct Library *SuperBase;           /* datatypes/picture.datatype */

static ULONG dispatch(Class *cl __asm("a0"), Object *o __asm("a2"), Msg msg __asm("a1"));

/* the superclass methods (no amiga.lib: these are what DoSuperMethodA does) */
static ULONG call_super(Class *cl, Object *o, Msg msg)
{
    Class *sc = cl->cl_Super;
    ULONG (*f)(Class *c __asm("a0"), Object *ob __asm("a2"), Msg m __asm("a1")) = (void *)sc->cl_Dispatcher.h_Entry;
    return f(sc, o, msg);
}

static ULONG dispatch(Class *cl __asm("a0"), Object *o __asm("a2"), Msg msg __asm("a1"))
{
    if (msg->MethodID == OM_NEW) {
        Object *n = (Object *)call_super(cl, o, msg);
        if (n && !dt_load(cl, n)) {
            struct Msg_ { ULONG id; } d = { OM_DISPOSE };
            LONG err = IoErr();
            /* OM_DISPOSE to our own dispatcher = the superclass's (we add nothing to dispose) */
            call_super(cl, n, (Msg)&d);
            SetIoErr(err ? err : ERROR_OBJECT_WRONG_TYPE);
            return 0;
        }
        return (ULONG)n;
    }
    return call_super(cl, o, msg);
}

struct DTBase *dt_open(struct DTBase *base);
struct DTBase *dt_open(struct DTBase *base)
{
    SysBase = base->sysbase;
    if (!base->cl) {
        if (!(DOSBase = (struct DosLibrary *)OpenLibrary((STRPTR)"dos.library", 39)) ||
            !(GfxBase = (struct GfxBase *)OpenLibrary((STRPTR)"graphics.library", 39)) ||
            !(IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39)) ||
            !(UtilityBase = OpenLibrary((STRPTR)"utility.library", 39)) ||
            !(DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39)) ||
            !(SuperBase = OpenLibrary((STRPTR)"datatypes/picture.datatype", 39)) ||
            (dt_c2p_init(), !dt_init()) ||
            !(base->cl = MakeClass(NULL, (STRPTR)"picture.datatype", NULL, 0, 0))) {
            dt_cleanup();
            if (SuperBase) CloseLibrary(SuperBase), SuperBase = NULL;
            if (DataTypesBase) CloseLibrary(DataTypesBase), DataTypesBase = NULL;
            if (UtilityBase) CloseLibrary(UtilityBase), UtilityBase = NULL;
            if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase), IntuitionBase = NULL;
            if (GfxBase) CloseLibrary((struct Library *)GfxBase), GfxBase = NULL;
            if (DOSBase) CloseLibrary((struct Library *)DOSBase), DOSBase = NULL;
            return NULL;
        }
        base->cl->cl_Dispatcher.h_Entry = (ULONG (*)())(void *)dispatch;
        base->cl->cl_UserData = (ULONG)base;
    }
    base->lib.lib_OpenCnt++;
    base->lib.lib_Flags &= ~LIBF_DELEXP;
    return base;
}

BPTR dt_expunge(struct DTBase *base);
BPTR dt_expunge(struct DTBase *base)
{
    BPTR seg;
    if (base->lib.lib_OpenCnt || (base->cl && !FreeClass(base->cl))) {   /* objects still alive */
        base->lib.lib_Flags |= LIBF_DELEXP;
        return 0;
    }
    base->cl = NULL;
    dt_cleanup();
    if (SuperBase) CloseLibrary(SuperBase), SuperBase = NULL;
    CloseLibrary(DataTypesBase); CloseLibrary(UtilityBase); CloseLibrary((struct Library *)IntuitionBase);
    CloseLibrary((struct Library *)GfxBase); CloseLibrary((struct Library *)DOSBase);
    seg = base->seglist;
    Remove((struct Node *)base);
    FreeMem((UBYTE *)base - base->lib.lib_NegSize, base->lib.lib_NegSize + base->lib.lib_PosSize);
    return seg;
}

BPTR dt_close(struct DTBase *base);
BPTR dt_close(struct DTBase *base)
{
    if (--base->lib.lib_OpenCnt == 0 && (base->lib.lib_Flags & LIBF_DELEXP)) return dt_expunge(base);
    return 0;
}

Class *dt_engine(struct DTBase *base);
Class *dt_engine(struct DTBase *base) { return base->cl; }

/* ---- the picture ------------------------------------------------------------------------- */

int dt_begin(struct DTPic *p, Object *o, int w, int h, int depth, int ncolours, UBYTE pal[][3])
{
    struct BitMapHeader *bmh = NULL;
    p->o = o; p->w = w; p->h = h; p->depth = depth;
    if (!(p->bm = AllocBitMap((ULONG)w, (ULONG)h, (ULONG)depth, BMF_CLEAR, NULL))) { SetIoErr(ERROR_NO_FREE_STORE); return 0; }
    if (!(p->pbuf = AllocVec((ULONG)(((w + 15) >> 4) << 1) * 8, MEMF_ANY))) { SetIoErr(ERROR_NO_FREE_STORE); return 0; }
    if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) && bmh) {
        bmh->bmh_Left = bmh->bmh_Top = 0;
        bmh->bmh_Width = (UWORD)w; bmh->bmh_Height = (UWORD)h;
        bmh->bmh_Depth = (UBYTE)depth;
        bmh->bmh_Masking = 0; bmh->bmh_Compression = 0; bmh->bmh_Transparent = 0;
        bmh->bmh_XAspect = 1; bmh->bmh_YAspect = 1;
        bmh->bmh_PageWidth = (WORD)w; bmh->bmh_PageHeight = (WORD)h;
    }
    SetDTAttrs(o, NULL, NULL, PDTA_NumColors, (ULONG)ncolours, TAG_DONE);
    dt_colours(p, ncolours, pal);
    return 1;
}

/* the colour registers (again, when a format learns its palette after the pixels: PCX) */
void dt_colours(struct DTPic *p, int ncolours, UBYTE pal[][3])
{
    struct ColorRegister *cmap = NULL;
    ULONG *cregs = NULL;
    int i;
    GetDTAttrs(p->o, PDTA_ColorRegisters, (ULONG)&cmap, PDTA_CRegs, (ULONG)&cregs, TAG_DONE);
    for (i = 0; i < ncolours; i++) {
        if (cmap) { cmap[i].red = pal[i][0]; cmap[i].green = pal[i][1]; cmap[i].blue = pal[i][2]; }
        if (cregs) {
            cregs[i * 3 + 0] = (ULONG)pal[i][0] * 0x01010101UL;
            cregs[i * 3 + 1] = (ULONG)pal[i][1] * 0x01010101UL;
            cregs[i * 3 + 2] = (ULONG)pal[i][2] * 0x01010101UL;
        }
    }
}

/* chunky -> planar with spread tables: SLO[v] has bit p of v (planes 0-3) at bit 8*p, SHI[v]
 * planes 4-7 likewise.  Shifting the two accumulators left once per pixel stacks 8 pixels'
 * bits into each byte lane (8 shifts never carry into the next lane), so a pixel costs two
 * lookups, two shifts and two ORs - against 8 shift/mask/or steps per plane before. */
static ULONG SLO[256], SHI[256];

void dt_c2p_init(void)
{
    int v, p;
    for (v = 0; v < 256; v++) {
        ULONG lo = 0, hi = 0;
        for (p = 0; p < 4; p++) {
            if (v & (1 << p)) lo |= 1UL << (8 * p);
            if (v & (16 << p)) hi |= 1UL << (8 * p);
        }
        SLO[v] = lo; SHI[v] = hi;
    }
}

void dt_row(struct DTPic *p, int y, const UBYTE *idx)
{
    struct BitMap *bm = p->bm;
    int x, depth = p->depth, w = p->w, full = w & ~7;
    ULONG off = (ULONG)y * bm->BytesPerRow + 0;
    UBYTE *pl0 = bm->Planes[0] + off, *pl1 = depth > 1 ? bm->Planes[1] + off : 0, *pl2 = depth > 2 ? bm->Planes[2] + off : 0;
    UBYTE *pl3 = depth > 3 ? bm->Planes[3] + off : 0, *pl4 = depth > 4 ? bm->Planes[4] + off : 0;
    UBYTE *pl5 = depth > 5 ? bm->Planes[5] + off : 0, *pl6 = depth > 6 ? bm->Planes[6] + off : 0;
    UBYTE *pl7 = depth > 7 ? bm->Planes[7] + off : 0;
    for (x = 0; x < w; x += 8) {
        ULONG lo = 0, hi = 0;
        const UBYTE *s = idx + x;
        int b = x >> 3;
        if (x < full && depth <= 4) {
            lo = SLO[s[0]];
            lo = (lo << 1) | SLO[s[1]]; lo = (lo << 1) | SLO[s[2]]; lo = (lo << 1) | SLO[s[3]];
            lo = (lo << 1) | SLO[s[4]]; lo = (lo << 1) | SLO[s[5]]; lo = (lo << 1) | SLO[s[6]];
            lo = (lo << 1) | SLO[s[7]];
        } else if (x < full) {
            lo = SLO[s[0]]; hi = SHI[s[0]];
            lo = (lo << 1) | SLO[s[1]]; hi = (hi << 1) | SHI[s[1]];
            lo = (lo << 1) | SLO[s[2]]; hi = (hi << 1) | SHI[s[2]];
            lo = (lo << 1) | SLO[s[3]]; hi = (hi << 1) | SHI[s[3]];
            lo = (lo << 1) | SLO[s[4]]; hi = (hi << 1) | SHI[s[4]];
            lo = (lo << 1) | SLO[s[5]]; hi = (hi << 1) | SHI[s[5]];
            lo = (lo << 1) | SLO[s[6]]; hi = (hi << 1) | SHI[s[6]];
            lo = (lo << 1) | SLO[s[7]]; hi = (hi << 1) | SHI[s[7]];
        } else {                                    /* the last, partial group */
            int k;
            for (k = 0; k < 8; k++) {
                UBYTE v = x + k < w ? s[k] : 0;
                lo = (lo << 1) | SLO[v]; hi = (hi << 1) | SHI[v];
            }
        }
        pl0[b] = (UBYTE)lo;
        if (pl1) pl1[b] = (UBYTE)(lo >> 8);
        if (pl2) pl2[b] = (UBYTE)(lo >> 16);
        if (pl3) pl3[b] = (UBYTE)(lo >> 24);
        if (pl4) pl4[b] = (UBYTE)hi;
        if (pl5) pl5[b] = (UBYTE)(hi >> 8);
        if (pl6) pl6[b] = (UBYTE)(hi >> 16);
        if (pl7) pl7[b] = (UBYTE)(hi >> 24);
    }
}

int dt_finish(struct DTPic *p, const char *name)
{
    ULONG mode = (p->w >= 640 ? HIRES_KEY : LORES_KEY) | (p->h >= 400 ? LACE : 0);
    if (p->pbuf) FreeVec(p->pbuf), p->pbuf = NULL;
    SetDTAttrs(p->o, NULL, NULL,
               DTA_ObjName, (ULONG)name,
               DTA_NominalHoriz, (ULONG)p->w, DTA_NominalVert, (ULONG)p->h,
               PDTA_BitMap, (ULONG)p->bm, PDTA_ModeID, mode, TAG_DONE);
    p->bm = NULL;                           /* picture.datatype owns it now */
    return 1;
}

void dt_abort(struct DTPic *p)
{
    if (p->pbuf) FreeVec(p->pbuf), p->pbuf = NULL;
    if (p->bm) { WaitBlit(); FreeBitMap(p->bm); p->bm = NULL; }
}

/* ---- the file and the decode, shared by every format ---- */

struct src { BPTR fh; UBYTE *buf; LONG pos, len; };

long dt_rd(void *h, unsigned char *dst, long n)
{
    struct src *s = h;
    long got = 0;
    while (got < n) {
        LONG k;
        if (s->pos >= s->len) {
            if ((s->len = Read(s->fh, s->buf, 16384)) <= 0) { s->len = 0; break; }
            s->pos = 0;
        }
        k = s->len - s->pos;
        if (k > n - got) k = n - got;
        CopyMem(s->buf + s->pos, dst + got, (ULONG)k);
        s->pos += k; got += k;
    }
    return got;
}

void *dt_al(unsigned long n) { return AllocVec(n ? n : 1, MEMF_ANY | MEMF_CLEAR); }
void dt_fr(void *p) { FreeVec(p); }

struct job { struct png_out out; struct DTPic pic; Object *o; };

static int job_begin(struct png_out *po)
{
    struct job *j = po->user;
    return dt_begin(&j->pic, j->o, po->w, po->h, po->depth, po->ncolours, po->pal);
}

static void job_row(struct png_out *po, int y, const unsigned char *idx)
{
    struct job *j = po->user;
    dt_row(&j->pic, y, idx);
}

static void job_colours(struct png_out *po)
{
    struct job *j = po->user;
    dt_colours(&j->pic, po->ncolours, po->pal);
}

static void job_row1(struct png_out *po, int y, const unsigned char *bits)
{
    struct job *j = po->user;
    struct BitMap *bm = j->pic.bm;
    CopyMem((APTR)bits, bm->Planes[0] + (ULONG)y * bm->BytesPerRow, (ULONG)(j->pic.w + 7) >> 3);
}

int dt_decode_file(Object *o, dt_dec_fn dec, const char *fallback_name)
{
    struct job *j;
    struct src s;
    BPTR fh = 0;
    ULONG st = 0;
    STRPTR name = NULL;
    int rc;
    GetDTAttrs(o, DTA_Handle, (ULONG)&fh, DTA_SourceType, (ULONG)&st, DTA_Name, (ULONG)&name, TAG_DONE);
    if (st != DTST_FILE || !fh) { SetIoErr(ERROR_NOT_IMPLEMENTED); return 0; }
    if (!(j = AllocVec(sizeof(*j), MEMF_ANY | MEMF_CLEAR))) { SetIoErr(ERROR_NO_FREE_STORE); return 0; }
    if (!(s.buf = AllocVec(16384, MEMF_ANY))) { FreeVec(j); SetIoErr(ERROR_NO_FREE_STORE); return 0; }
    s.fh = fh; s.pos = s.len = 0;
    Seek(fh, 0, OFFSET_BEGINNING);
    j->o = o;
    j->out.begin = job_begin; j->out.row = job_row; j->out.row1 = job_row1; j->out.colours = job_colours; j->out.user = j;
    rc = dec(dt_rd, &s, &j->out);
    FreeVec(s.buf);
    if (rc == PNG_OK) {
        dt_finish(&j->pic, name ? (const char *)FilePart(name) : fallback_name);
        FreeVec(j);
        return 1;
    }
    dt_abort(&j->pic);
    FreeVec(j);
    SetIoErr(rc == PNG_NOMEM ? ERROR_NO_FREE_STORE : ERROR_OBJECT_WRONG_TYPE);
    return 0;
}
