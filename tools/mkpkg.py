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
    # The icons are finished .info files in install/ (made once with the author's icon
    # writer; to change one, regenerate it there and commit the new file). The drawer's
    # own icon goes BESIDE the drawer, or Workbench doesn't show it.
    for f in ('ReadMe.info', 'Install_AmiDT.info'):
        copy(os.path.join(ROOT, 'install', f), os.path.join(OUT, f))
    copy(os.path.join(ROOT, 'install', 'AmiDT.info'), OUT + '.info')
    n = sum(len(fs) for _, _, fs in os.walk(os.path.dirname(OUT)))
    size = sum(os.path.getsize(os.path.join(b, f)) for b, _, fs in os.walk(os.path.dirname(OUT)) for f in fs)
    print('package: %s  (%d files, %d KB)' % (OUT, n, size // 1024))

if __name__ == '__main__':
    main()
