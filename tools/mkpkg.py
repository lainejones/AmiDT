"""mkpkg.py - the AmiDT release drawer: out/release/AmiDT/ (+ AmiDT.info beside it).

    make all && python tools/mkpkg.py

AmiDT/
  Install_AmiDT (+ icon: Installer project)   ReadMe (+ icon)
  Classes/DataTypes/<fmt>.datatype            the 7 classes
  DEVS/DataTypes/<NAME>                       descriptors (PNG/GIF/BMP only installed where missing: OS 3.0/3.1)
"""
import os, shutil, sys
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TOOLS = r'C:\projects\tools'
sys.path.insert(0, HERE)
from mkclasses import FORMATS

OUT = os.path.join(ROOT, 'out', 'release', 'AmiDT')
NEW = ('PNG', 'GIF', 'BMP', 'TGA', 'PCX', 'ICO', 'QOI')   # all: OS 3.0/3.1 have no PNG/GIF/BMP either

def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copyfile(src, dst)

def main():
    if os.path.exists(os.path.dirname(OUT)):
        shutil.rmtree(os.path.dirname(OUT))
    for fmt, name, *_ in FORMATS:
        src = os.path.join(ROOT, 'out', '%s.datatype' % fmt)
        if not os.path.isfile(src): sys.exit('%s missing - run make all first' % src)
        copy(src, os.path.join(OUT, 'Classes', 'DataTypes', '%s.datatype' % fmt))
        if name in NEW:
            copy(os.path.join(ROOT, 'out', 'DataTypes', name), os.path.join(OUT, 'DEVS', 'DataTypes', name))
    for f in ('Install_AmiDT', 'ReadMe'):
        copy(os.path.join(ROOT, 'install', f), os.path.join(OUT, f))
    copy(os.path.join(ROOT, 'LICENSE'), os.path.join(OUT, 'LICENSE'))
    sys.path.insert(0, TOOLS)
    import iconlib, makeicon_doc as doc, makeicon_drawer as drw, makeicon_install as mi
    open(OUT + '.info', 'wb').write(
        iconlib.build_info(drw.build_cidx(), drw.PALETTE, drw.PLANAR_MAP, drw.W, drw.H, drw.TRANSPARENT, drawer=True))
    open(os.path.join(OUT, 'ReadMe.info'), 'wb').write(
        iconlib.build_info(doc.build_cidx(), doc.PALETTE, doc.PLANAR_MAP, doc.W, doc.H, doc.TRANSPARENT,
                           icon_type=4, default_tool=doc.DEFAULT_TOOL))
    open(os.path.join(OUT, 'Install_AmiDT.info'), 'wb').write(
        iconlib.build_info(mi.build_cidx(), mi.PALETTE, mi.PLANAR_MAP, mi.W, mi.H, mi.TRANSPARENT,
                           icon_type=4, default_tool='Installer',
                           tool_types=['APPNAME=AmiDT', 'SCRIPT=Install_AmiDT', 'DEFUSER=AVERAGE',
                                       'MINUSER=AVERAGE', 'LOG=FALSE']))
    n = sum(len(fs) for _, _, fs in os.walk(os.path.dirname(OUT)))
    size = sum(os.path.getsize(os.path.join(b, f)) for b, _, fs in os.walk(os.path.dirname(OUT)) for f in fs)
    print('package: %s  (%d files, %d KB)' % (OUT, n, size // 1024))

if __name__ == '__main__':
    main()
