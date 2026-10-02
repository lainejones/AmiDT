# AmiDT - fast, small datatypes.  Build under WSL with bebbo's amiga-gcc on PATH:
#   wsl -e bash -lc 'export PATH=/opt/amiga/bin:$PATH; cd /mnt/c/projects/AmiDT && make'
# A datatype is a library with no C startup: -nostartfiles -nostdlib, its own ROM tag
# (src/dt/dt_start.S).  68020 build (32-bit divides are native; no FPU anywhere).

CC      = m68k-amigaos-gcc
CPU     = -m68020
CFLAGS  = $(CPU) -O2 -Wall -Wno-pointer-sign -fomit-frame-pointer -msoft-float \
          -nostartfiles -nostdlib -fno-builtin
DATE    := $(shell date +%-d.%-m.%Y)
DTVER   = 47
DTREV   = 100
DTDEFS  = -DDT_VERSION=$(DTVER) -DDT_REVISION=$(DTREV) -DDT_VERSTR='"$(DTVER).$(DTREV)"' -DDT_DATE='"$(DATE)"'
# the cores each class links (inflate + png for ICO: icons may hold PNGs)
CORE_png = src/core/pngdec.c src/core/inflate.c src/core/quant.c
CORE_gif = src/core/gifdec.c
CORE_bmp = src/core/bmpdec.c src/core/quant.c
CORE_tga = src/core/tgadec.c src/core/quant.c
CORE_pcx = src/core/pcxdec.c src/core/quant.c
CORE_qoi = src/core/qoidec.c src/core/quant.c
CORE_ico = src/core/icodec.c src/core/bmpdec.c src/core/pngdec.c src/core/inflate.c src/core/quant.c
FMTS     = png gif bmp tga pcx ico qoi

DTCOMMON = src/dt/dt_start.S src/dt/dt_lib.c
DTDEPS   = $(DTCOMMON) src/dt/dt_lib.h src/core/*.h

all: $(FMTS:%=out/%.datatype) out/dtbench out/DataTypes/PNG

# every class also builds under a benchmark name (amidt<fmt>.datatype): a class library stays in
# memory under its name, so the benchmark copy can't be mistaken for the system's class
bench: $(FMTS:%=out/amidt%.datatype)

# (two rules, not one with two targets: that would mean "one recipe makes both")
.SECONDEXPANSION:
out/amidt%.datatype: src/dt/dt_$$*.c $$(CORE_$$*) $(DTDEPS)
	@mkdir -p out
	$(CC) $(CFLAGS) $(DTDEFS) -DDT_LIBNAME='"$(notdir $@)"' -s -o $@ $(DTCOMMON) src/dt/dt_$*.c $(CORE_$*)
out/%.datatype: src/dt/dt_$$*.c $$(CORE_$$*) $(DTDEPS)
	@mkdir -p out
	$(CC) $(CFLAGS) $(DTDEFS) -DDT_LIBNAME='"$(notdir $@)"' -s -o $@ $(DTCOMMON) src/dt/dt_$*.c $(CORE_$*)

# the class sources (src/dt/dt_<fmt>.c) and DEVS:DataTypes descriptors come from one table
out/DataTypes/PNG: tools/mkclasses.py tools/dtd.py
	python3 tools/mkclasses.py

out/dtbench: src/dtbench.c
	@mkdir -p out
	$(CC) -O2 -m68020 -msoft-float -noixemul -Wall -Wno-pointer-sign -fomit-frame-pointer -s -o $@ $< -lm

test/host_png: test/host_png.c src/core/*.c src/core/*.h
	gcc -O2 -Wall -o $@ test/host_png.c src/core/*.c

clean:
	rm -rf out test/host_png
