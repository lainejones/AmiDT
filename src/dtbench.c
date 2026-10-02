/*
 * dtbench - how fast does a datatype open a file?  (AmiDT test tool)
 *
 *   dtbench FILE/M [REPEAT/N] [LAYOUT/S]
 *
 * For each file: NewDTObject() (the datatype reads and decodes it), optionally
 * DTM_PROCLAYOUT (what MultiView does before it can draw), DisposeDTObject().
 * Timed with the E-clock; REPEAT runs it n times and prints the best and the
 * average, so disk caching and the first-load of the class are visible.
 * Also prints which datatype took it, the size/depth, and the memory it held.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <datatypes/soundclass.h>
#include <intuition/classusr.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/datatypes.h>
#include <proto/intuition.h>
#include <proto/timer.h>
#include <stdio.h>
#include <string.h>
#include <exec/memory.h>

static const char __attribute__((used)) verstag[] = "$VER: dtbench 1.1 (1.10.2026)";

struct Library *DataTypesBase;
struct IntuitionBase *IntuitionBase;
struct Device *TimerBase;
static struct timerequest treq;
static ULONG efreq;

static double now_ms(void)
{
    struct EClockVal ev;
    efreq = ReadEClock(&ev);
    return ((double)ev.ev_hi * 4294967296.0 + (double)ev.ev_lo) * 1000.0 / (double)efreq;
}

/* CLASS=<file>: time that datatype class instead of the one the system would pick, without
 * installing it - its library is opened by path and the object is made the way
 * datatypes.library makes one (the file handle and the system's DataType for the file) */
static struct Library *classlib;
static Class *engine;

static Class *obtain_engine(struct Library *lib)        /* LVO -30 of a datatype class library */
{
    register Class *res __asm("d0");
    register struct Library *a6 __asm("a6") = lib;
    __asm volatile ("jsr a6@(-30:W)" : "=r"(res) : "r"(a6) : "d1", "a0", "a1", "fp0", "fp1", "cc", "memory");
    return res;
}

static Object *open_obj(const char *path, BPTR *fh, struct DataType **dtn)
{
    BPTR lk;
    *fh = 0; *dtn = NULL;
    if (!engine) return NewDTObject((APTR)path, PDTA_Remap, FALSE, TAG_DONE);
    if (!(lk = Lock((STRPTR)path, ACCESS_READ))) return NULL;
    *dtn = ObtainDataTypeA(DTST_FILE, (APTR)lk, NULL);
    UnLock(lk);
    if (!(*fh = Open((STRPTR)path, MODE_OLDFILE))) return NULL;
    return (Object *)NewObject(engine, NULL, DTA_Name, (ULONG)path, DTA_SourceType, DTST_FILE,
                               DTA_Handle, (ULONG)*fh, DTA_DataType, (ULONG)*dtn, PDTA_Remap, FALSE, TAG_DONE);
}

/* DUMP=<file>: the picture as the datatype made it, in test/host_png's "AIDX" format (the
 * size, the palette, and the 8-bit indices read back out of PDTA_BitMap's planes), so the
 * Amiga build can be compared byte for byte with the PC one.  V40-style (8-bit planar) only. */
static const char *dumpto;

static void dump(Object *o, const char *path)
{
    struct BitMapHeader *bmh = NULL;
    struct BitMap *bm = NULL;
    struct ColorRegister *cmap = NULL;
    ULONG ncol = 0;
    BPTR f;
    LONG hdr[4];
    UBYTE pal[768];
    int x, y, p, i;
    if (!GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, PDTA_BitMap, (ULONG)&bm, PDTA_NumColors, (ULONG)&ncol,
                    PDTA_ColorRegisters, (ULONG)&cmap, TAG_DONE) || !bmh) { PutStr((STRPTR)"dump: no header\n"); return; }
    if (!bm) GetDTAttrs(o, PDTA_DestBitMap, (ULONG)&bm, TAG_DONE);     /* OS 3.2: only after layout */
    if (!bm) { PutStr((STRPTR)"dump: no bitmap (use LAYOUT)\n"); return; }
    if (!(f = Open((STRPTR)path, MODE_NEWFILE))) return;
    /* little-endian ints, as the PC harness writes them */
    hdr[0] = bmh->bmh_Width; hdr[1] = bmh->bmh_Height; hdr[2] = (LONG)ncol; hdr[3] = bmh->bmh_Depth;
    Write(f, "AIDX", 4);
    for (i = 0; i < 4; i++) { UBYTE le[4] = { (UBYTE)hdr[i], (UBYTE)(hdr[i] >> 8), (UBYTE)(hdr[i] >> 16), (UBYTE)(hdr[i] >> 24) }; Write(f, le, 4); }
    for (i = 0; i < 768; i++) pal[i] = 0;
    for (i = 0; i < (int)ncol && i < 256 && cmap; i++) { pal[i * 3] = cmap[i].red; pal[i * 3 + 1] = cmap[i].green; pal[i * 3 + 2] = cmap[i].blue; }
    Write(f, pal, 768);
    {
        UBYTE *row = AllocVec(bmh->bmh_Width, MEMF_ANY);
        if (row) {
            for (y = 0; y < bmh->bmh_Height; y++) {
                for (x = 0; x < bmh->bmh_Width; x++) {
                    UBYTE v = 0;
                    for (p = 0; p < bm->Depth; p++)
                        if (bm->Planes[p][y * bm->BytesPerRow + (x >> 3)] & (0x80 >> (x & 7))) v |= 1 << p;
                    row[x] = v;
                }
                Write(f, row, bmh->bmh_Width);
            }
            FreeVec(row);
        }
    }
    Close(f);
}

static int one(const char *path, int layout, double *ms, char *what, LONG *mem)
{
    Object *o;
    double t0;
    BPTR fh;
    struct DataType *dtn;
    LONG before = AvailMem(MEMF_ANY);
    t0 = now_ms();
    o = open_obj(path, &fh, &dtn);
    if (!o) { if (fh) Close(fh); if (dtn) ReleaseDataType(dtn); return 0; }
    if (layout) DoDTMethod(o, NULL, NULL, DTM_PROCLAYOUT, 0, TRUE);
    *ms = now_ms() - t0;
    *mem = before - AvailMem(MEMF_ANY);
    if (what) {
        struct DataType *dt = NULL;
        struct BitMapHeader *bmh = NULL;
        ULONG frames = 0, samples = 0;
        what[0] = 0;
        GetDTAttrs(o, DTA_DataType, (ULONG)&dt, TAG_DONE);
        if (dt) sprintf(what, "%s/%s", dt->dtn_Header->dth_GroupID == GID_PICTURE ? "picture" :
                        dt->dtn_Header->dth_GroupID == GID_SOUND ? "sound" :
                        dt->dtn_Header->dth_GroupID == GID_TEXT ? "text" :
                        dt->dtn_Header->dth_GroupID == GID_ANIMATION ? "animation" : "other",
                        dt->dtn_Header->dth_Name);
        if (GetDTAttrs(o, PDTA_BitMapHeader, (ULONG)&bmh, TAG_DONE) && bmh)
            sprintf(what + strlen(what), " %lux%lu x%lu", (unsigned long)bmh->bmh_Width,
                    (unsigned long)bmh->bmh_Height, (unsigned long)bmh->bmh_Depth);
        else if (GetDTAttrs(o, SDTA_SampleLength, (ULONG)&samples, TAG_DONE))
            sprintf(what + strlen(what), " %lu samples", (unsigned long)samples);
        (void)frames;
        if (engine && what[0] == 0) strcpy(what, "(class)");
        if (engine) { char *s = what + strlen(what); sprintf(s, " [%s]", classlib->lib_Node.ln_Name); }
        if (dumpto) dump(o, dumpto);
    }
    DisposeDTObject(o);
    if (fh) Close(fh);
    if (dtn) ReleaseDataType(dtn);
    return 1;
}

int main(void)
{
    LONG args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rd = ReadArgs((STRPTR)"FILE/M/A,REPEAT/N,LAYOUT/S,CLASS/K,DUMP/K", args, NULL);
    STRPTR *files;
    int rep, rc = 0;
    if (!rd) { PrintFault(IoErr(), (STRPTR)"dtbench"); return 20; }
    files = (STRPTR *)args[0];
    rep = args[1] ? *(LONG *)args[1] : 3;
    dumpto = (const char *)args[4];
    if (rep < 1) rep = 1;
    if (!(DataTypesBase = OpenLibrary((STRPTR)"datatypes.library", 39))) { PutStr((STRPTR)"no datatypes.library\n"); FreeArgs(rd); return 20; }
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (OpenDevice((STRPTR)TIMERNAME, UNIT_ECLOCK, (struct IORequest *)&treq, 0)) {
        PutStr((STRPTR)"no timer.device\n"); CloseLibrary(DataTypesBase); FreeArgs(rd); return 20;
    }
    TimerBase = treq.tr_node.io_Device;
    if (args[3]) {
        if (!(classlib = OpenLibrary((STRPTR)args[3], 0)) || !(engine = obtain_engine(classlib))) {
            Printf((STRPTR)"can't open the class %s\n", args[3]);
            if (classlib) CloseLibrary(classlib);
            CloseDevice((struct IORequest *)&treq); CloseLibrary(DataTypesBase); FreeArgs(rd); return 20;
        }
    }
    for (; *files; files++) {
        double best = 1e30, sum = 0, ms;
        char what[120];
        LONG mem = 0, m;
        int i, ok = 1;
        for (i = 0; i < rep; i++) {
            if (!one((char *)*files, args[2] != 0, &ms, i == 0 ? what : NULL, &m)) { ok = 0; break; }
            if (ms < best) best = ms;
            sum += ms;
            if (i == 0) mem = m;
        }
        if (!ok) { Printf((STRPTR)"%-30s  no datatype could open it (error %ld)\n", (LONG)FilePart(*files), IoErr()); rc = 5; continue; }
        {
            char line[200];
            sprintf(line, "%-30s %-34s best %8.1f ms  avg %8.1f ms  mem %6ld KB\n",
                    (char *)FilePart(*files), what, best, sum / rep, (long)(mem / 1024));
            PutStr((STRPTR)line);
        }
    }
    if (classlib) CloseLibrary(classlib);
    CloseDevice((struct IORequest *)&treq);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    CloseLibrary(DataTypesBase);
    FreeArgs(rd);
    return rc;
}
