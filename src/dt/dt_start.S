| dt_start.S - the ROM tag and jump table of an AmiDT datatype class library.
| GNU as, Motorola syntax.  Same pattern as a314bsd's lib_start.S: rt_Init points at our
| own Init (MakeLibrary + AddLibrary), no RTF_AUTOINIT.  The C side (dt_lib.c + the format's
| file) supplies _dt_open/_dt_close/_dt_expunge/_dt_engine and the names below come from
| the build (-DDT_NAME="png.datatype" ...), see the Makefile.

    .equ    LN_TYPE,        8
    .equ    LN_NAME,        10
    .equ    LIB_FLAGS,      14
    .equ    LIB_NEGSIZE,    16
    .equ    LIB_POSSIZE,    18
    .equ    LIB_VERSION,    20
    .equ    LIB_REVISION,   22
    .equ    LIB_IDSTRING,   24
    .equ    LIBF_CHANGED,   2
    .equ    LIBF_SUMUSED,   4
    .equ    NT_LIBRARY,     9

    .equ    base_SysBase,   34
    .equ    base_SegList,   38
    .equ    base_SIZEOF,    48          | struct DTBase in dt_lib.h (34 + 14, rounded)

    .equ    NEGSIZE,        30          | 5 vectors * 6

    .text
Start:
    moveq   #-1,d0
    rts
    .even

RomTag:
    .word   0x4AFC
    .long   RomTag
    .long   EndSkip
    .byte   0                           | no RTF_AUTOINIT
    .byte   DT_VERSION
    .byte   NT_LIBRARY
    .byte   0
    .long   _dt_libname
    .long   _dt_idstring
    .long   Init
EndSkip:
    .even

FuncTable:
    .long   Open
    .long   Close
    .long   Expunge
    .long   Null
    .long   Engine                      | -30 ObtainEngine
    .long   -1

| ---- Init: d0 = segList, a6 = SysBase ----------------------------------------------
Init:
    movem.l d2-d7/a2-a6,-(sp)
    move.l  d0,d6
    lea     FuncTable(pc),a0
    sub.l   a1,a1
    sub.l   a2,a2
    move.l  #base_SIZEOF,d0
    move.l  d6,d1
    jsr     -84(a6)                     | MakeLibrary
    move.l  d0,d5
    beq.s   .fail
    movea.l d5,a1
    move.l  a6,base_SysBase(a1)
    move.l  d6,base_SegList(a1)
    move.b  #NT_LIBRARY,LN_TYPE(a1)
    move.l  #_dt_libname,LN_NAME(a1)
    move.l  #_dt_idstring,LIB_IDSTRING(a1)
    move.b  #LIBF_CHANGED+LIBF_SUMUSED,LIB_FLAGS(a1)
    move.w  #DT_VERSION,LIB_VERSION(a1)
    move.w  #DT_REVISION,LIB_REVISION(a1)
    move.l  d5,-(sp)
    jsr     -396(a6)                    | AddLibrary
    move.l  (sp)+,d0
    bra.s   .done
.fail:
    moveq   #0,d0
.done:
    movem.l (sp)+,d2-d7/a2-a6
    rts

| ---- Open: a6 = our base ------------------------------------------------------------
Open:
    movem.l d1-d7/a0-a5,-(sp)
    move.l  a6,-(sp)
    jsr     _dt_open                    | -> base or NULL
    addq.l  #4,sp
    movem.l (sp)+,d1-d7/a0-a5
    rts

Close:
    movem.l d1-d7/a0-a5,-(sp)
    move.l  a6,-(sp)
    jsr     _dt_close                   | -> segList if it was expunged, else 0
    addq.l  #4,sp
    movem.l (sp)+,d1-d7/a0-a5
    rts

Expunge:
    movem.l d1-d7/a0-a5,-(sp)
    move.l  a6,-(sp)
    jsr     _dt_expunge
    addq.l  #4,sp
    movem.l (sp)+,d1-d7/a0-a5
    rts

Null:
    moveq   #0,d0
    rts

Engine:
    move.l  a6,-(sp)
    jsr     _dt_engine                  | -> Class *
    addq.l  #4,sp
    rts
