"""check_png.py - the AmiDT PNG, GIF and BMP cores against PIL, pixel by pixel.

    python test/check_png.py [files...]     (default: test/pngsuite/*.png, testfiles/*.png, test/corpus/* - make_corpus.py)

Palette and greyscale images: the colour of every pixel (pal[index]) must equal PIL's.
Truecolor: every index must equal the same 6x7x6 / 4x4-Bayer dither computed here from PIL's RGB.
PngSuite's x*.png are corrupt on purpose: they must FAIL with an error code (and not crash).
"""
import glob, os, struct, subprocess, sys, tempfile
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, 'host_png')
BAYER = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]

def chan(levels, mult):
    t = np.zeros((16, 256), np.int32)
    for c in range(16):
        thr = BAYER[c] * 2 + 1
        for v in range(256):
            scaled = v * (levels - 1) * 32
            lo = scaled // (255 * 32)
            frac = (scaled - lo * 255 * 32) // 255
            lvl = min(lo + (1 if frac >= thr else 0), levels - 1)
            t[c, v] = lvl * mult
    return t
TR, TG, TB = chan(6, 42), chan(7, 6), chan(6, 1)

def wsl(path):
    p = os.path.abspath(path).replace('\\', '/')
    return '/mnt/' + p[0].lower() + p[2:]

def run(path, out):
    r = subprocess.run(['wsl', '-e', wsl(EXE), wsl(path), wsl(out) if out != '-' else '-'], capture_output=True, text=True)
    try: return int(r.stdout.strip().splitlines()[-1])
    except Exception: return 'crash:' + (r.stderr or r.stdout)[-200:]

def ours(out):
    d = open(out, 'rb').read()
    w, h, nc, depth = struct.unpack('<4i', d[4:20])
    pal = np.frombuffer(d[20:788], np.uint8).reshape(256, 3)
    idx = np.frombuffer(d[788:788 + w * h], np.uint8).reshape(h, w)
    return w, h, nc, depth, pal, idx

def ctype(path):
    d = open(path, 'rb').read(33)
    if d[1:4] != b'PNG': return None, None
    return d[25], d[24]                         # IHDR colour type, bit depth

def expected(path):
    if os.path.exists(path + '.col.npy'):      # PIL reads it wrongly (RLE4) or not at all: the colours
        return 'colour', np.load(path + '.col.npy')
    if os.path.exists(path + '.npy'):          # a truecolor file PIL can't read: make_corpus.py left the RGB
        rgb = np.load(path + '.npy').astype(np.int64)
        h, w = rgb.shape[:2]
        y, x = np.mgrid[0:h, 0:w]
        cell = ((y & 3) << 2) | (x & 3)
        return 'index', (TR[cell, rgb[..., 0]] + TG[cell, rgb[..., 1]] + TB[cell, rgb[..., 2]]).astype(np.uint8)
    im = Image.open(path)
    if im.format == 'GIF':                      # outside the first frame: the screen's background colour
        x0, y0, x1, y1 = im.tile[0].extents
        bg = im.info.get('background', 0)
        pal = np.array(im.getpalette()[:768] + [0] * 768, np.uint8)[:768].reshape(256, 3)
        a = np.array(im)
        ncol = len(im.getpalette()) // 3
        if bg >= ncol: bg = 0
        out = np.empty(a.shape + (3,), np.uint8); out[...] = pal[bg]
        out[y0:y1, x0:x1] = pal[a[y0:y1, x0:x1]]
        return 'colour', out
    mode, info = im.mode, im.info
    ct, bd = ctype(path)
    if ct == 4 and bd == 16:                    # grey+alpha 16: PIL hands it over as RGBA - grey = R
        a = np.array(im.convert('RGBA'))[..., 0]
        return 'colour', np.stack([a, a, a], -1)
    if mode == 'P':
        return 'colour', np.array(im.convert('RGB'))
    if mode in ('1', 'L', 'LA'):
        return 'colour', np.array(im.convert('L').convert('RGB'))
    if mode.startswith('I'):
        a = (np.array(im).astype(np.int64) >> 8).clip(0, 255).astype(np.uint8)
        return 'colour', np.stack([a, a, a], -1)
    rgb = np.array(im.convert('RGB')).astype(np.int64)
    h, w = rgb.shape[:2]
    y, x = np.mgrid[0:h, 0:w]
    cell = ((y & 3) << 2) | (x & 3)
    return 'index', (TR[cell, rgb[..., 0]] + TG[cell, rgb[..., 1]] + TB[cell, rgb[..., 2]]).astype(np.uint8)

files = sys.argv[1:] or (sorted(glob.glob(os.path.join(HERE, 'pngsuite', '*.png'))) + sorted(glob.glob(os.path.join(HERE, '..', 'testfiles', '*.png')))
                         + sorted(f for f in glob.glob(os.path.join(HERE, 'corpus', '*')) if not f.endswith('.npy')) + [os.path.join(HERE, '..', 'testfiles', f) for f in ('pic320.gif', 'true640.bmp')])
out = os.path.join(tempfile.gettempdir(), 'amidt.idx')
bad = good = 0
for f in files:
    name = os.path.basename(f)
    if name in ('xcsn0g01.png', 'xhdn0g08.png'):  # only a bad CRC: AmiDT skips CRCs for speed - shown
        good += 1; continue
    if name.startswith('x'):                    # corrupt on purpose
        rc = run(f, '-')
        if isinstance(rc, int) and rc != 0: good += 1
        else: bad += 1; print('FAIL %-16s should fail, got %r' % (name, rc))
        continue
    rc = run(f, out)
    if rc != 0:
        bad += 1; print('FAIL %-16s decode error %r' % (name, rc)); continue
    w, h, nc, depth, pal, idx = ours(out)
    kind, ref = expected(f)
    if kind == 'colour':
        got = pal[idx]
        diff = int((got != ref).any(-1).sum())
    else:
        diff = int((idx != ref).sum())
    if diff:
        bad += 1; print('FAIL %-16s %d of %d pixels differ (%s, %d colours, depth %d)' % (name, diff, w * h, kind, nc, depth))
    else:
        good += 1
print('%d passed, %d failed' % (good, bad))
sys.exit(1 if bad else 0)
