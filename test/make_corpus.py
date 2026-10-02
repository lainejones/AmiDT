"""make_corpus.py - GIF and BMP test files for the AmiDT cores (test/corpus/), checked by check_png.py.

PIL writes the common kinds; the rest (RLE4/RLE8, 16-bit 555/565, 4-bit, top-down, OS/2 headers,
GIF frames smaller than the screen, local colour tables) are written by hand here.
"""
import os, random, struct
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'corpus')
os.makedirs(OUT, exist_ok=True)
rnd = random.Random(1)

def photo(w, h):
    y, x = np.mgrid[0:h, 0:w]
    r = (128 + 127 * np.sin(x / 23.0 + y / 41.0)).astype(np.uint8)
    g = (128 + 127 * np.sin(y / 17.0 - x / 59.0)).astype(np.uint8)
    b = ((x * 3 + y * 5) & 255).astype(np.uint8)
    a = np.stack([r, g, b], -1)
    a = np.clip(a.astype(int) + np.random.RandomState(w * h).randint(-12, 13, a.shape), 0, 255).astype(np.uint8)
    return Image.fromarray(a, 'RGB')

def pal_image(w, h, n):
    return photo(w, h).quantize(n)

# ---- GIF via PIL
for (w, h) in [(1, 1), (7, 5), (33, 17), (320, 200), (640, 480)]:
    for n in (2, 4, 16, 256):
        im = pal_image(w, h, n)
        im.save(os.path.join(OUT, 'g_%dx%d_%d.gif' % (w, h, n)), interlace=False)
        im.save(os.path.join(OUT, 'g_%dx%d_%d_i.gif' % (w, h, n)), interlace=True)
frames = [pal_image(64, 48, 16), pal_image(64, 48, 16).transpose(Image.FLIP_LEFT_RIGHT)]
frames[0].save(os.path.join(OUT, 'g_anim.gif'), save_all=True, append_images=frames[1:], duration=100, loop=0)

# ---- GIF by hand: a frame on a bigger screen, a local colour table, no global table
def lzw_raw(pixels, minsize):
    """uncompressed LZW: a clear code often enough that the code size never grows"""
    clear, eoi, size = 1 << minsize, (1 << minsize) + 1, minsize + 1
    codes, run = [clear], 0
    for p in pixels:
        codes.append(p); run += 1
        if run == (1 << minsize) - 3: codes.append(clear); run = 0
    codes.append(eoi)
    bits = nb = 0; out = bytearray()
    for c in codes:
        bits |= c << nb; nb += size
        while nb >= 8: out.append(bits & 255); bits >>= 8; nb -= 8
    if nb: out.append(bits & 255)
    blocks = bytearray()
    for i in range(0, len(out), 255):
        blocks += bytes([len(out[i:i + 255])]) + out[i:i + 255]
    return bytes([minsize]) + bytes(blocks) + b'\0'

def gif_by_hand(name, sw, sh, fx, fy, fw, fh, gct, lct, bg):
    pix = [rnd.randrange(len(lct or gct)) for _ in range(fw * fh)]
    d = bytearray(b'GIF89a') + struct.pack('<HH', sw, sh)
    if gct:
        bits = (len(gct) - 1).bit_length()
        d += bytes([0x80 | (bits - 1), bg, 0]) + b''.join(bytes(c) for c in gct)
    else:
        d += bytes([0, bg, 0])
    d += b'\x21\xf9\x04\x00\x00\x00\x00\x00'                       # a graphic control extension
    d += b'\x2c' + struct.pack('<HHHH', fx, fy, fw, fh)
    if lct:
        bits = (len(lct) - 1).bit_length()
        d += bytes([0x80 | (bits - 1)]) + b''.join(bytes(c) for c in lct)
    else:
        d += b'\0'
    d += lzw_raw(pix, max(2, (len(lct or gct) - 1).bit_length())) + b'\x3b'
    open(os.path.join(OUT, name), 'wb').write(d)

G16 = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(16)]
L4 = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0)]
gif_by_hand('h_frame.gif', 100, 80, 10, 20, 50, 30, G16, None, 3)
gif_by_hand('h_lct.gif', 40, 30, 0, 0, 40, 30, G16, L4, 0)
gif_by_hand('h_nogct.gif', 40, 30, 0, 0, 40, 30, None, L4, 0)
gif_by_hand('h_corner.gif', 64, 64, 32, 32, 32, 32, G16, None, 5)

# ---- BMP via PIL: 1-bit, 8-bit grey + palette, 24, 32
for (w, h) in [(1, 1), (7, 5), (33, 17), (640, 480)]:
    photo(w, h).convert('1').save(os.path.join(OUT, 'b_%dx%d_1.bmp' % (w, h)))
    photo(w, h).convert('L').save(os.path.join(OUT, 'b_%dx%d_8g.bmp' % (w, h)))
    pal_image(w, h, 256).save(os.path.join(OUT, 'b_%dx%d_8p.bmp' % (w, h)))
    photo(w, h).save(os.path.join(OUT, 'b_%dx%d_24.bmp' % (w, h)))
    photo(w, h).convert('RGBA').save(os.path.join(OUT, 'b_%dx%d_32.bmp' % (w, h)))

# ---- BMP by hand
def bmp(name, w, h, bpp, rows, pal=None, comp=0, masks=None, hsize=40, topdown=False, data=None, used=0):
    """rows: list of bytes per row, top first (already packed for bpp); data overrides (RLE)"""
    if data is None:
        stride = ((w * bpp + 31) // 32) * 4
        order = rows if topdown else rows[::-1]
        data = b''.join(r + b'\0' * (stride - len(r)) for r in order)
    pe = 3 if hsize == 12 else 4
    paldata = b''.join(bytes([c[2], c[1], c[0]] + ([0] if pe == 4 else [])) for c in (pal or []))
    maskdata = b''.join(struct.pack('<I', m) for m in masks) if (masks and hsize == 40) else b''
    if hsize == 12:
        ih = struct.pack('<IHHHH', 12, w, h, 1, bpp)
    else:
        ih = struct.pack('<IiiHHIIiiII', hsize, w, -h if topdown else h, 1, bpp, comp, len(data), 2835, 2835, used, 0)
        if hsize > 40:
            ih += struct.pack('<III', *(masks or (0, 0, 0))) + b'\0' * (hsize - 52)
    off = 14 + len(ih) + len(maskdata) + len(paldata)
    fh = b'BM' + struct.pack('<IHHI', off + len(data), 0, 0, off)
    open(os.path.join(OUT, name), 'wb').write(fh + ih + maskdata + paldata + data)

def pack(idx, bpp):
    out = bytearray(); acc = n = 0
    for v in idx:
        acc = (acc << bpp) | v; n += bpp
        if n == 8: out.append(acc); acc = n = 0
    if n: out.append(acc << (8 - n))
    return bytes(out)

W, H = 37, 23
P16 = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(16)]
P256 = [(rnd.randrange(256), rnd.randrange(256), rnd.randrange(256)) for _ in range(256)]
I16 = [[rnd.randrange(16) for _ in range(W)] for _ in range(H)]
I256 = [[rnd.randrange(256) for _ in range(W)] for _ in range(H)]
bmp('m_4.bmp', W, H, 4, [pack(r, 4) for r in I16], P16)
bmp('m_4_used.bmp', W, H, 4, [pack([v % 6 for v in r], 4) for r in I16], P16[:6], used=6)
bmp('m_8_top.bmp', W, H, 8, [bytes(r) for r in I256], P256, topdown=True)
bmp('m_os2_8.bmp', W, H, 8, [bytes(r) for r in I256], P256, hsize=12)
bmp('m_os2_24.bmp', W, H, 24, [bytes(v for x in range(W) for v in (x * 7 & 255, y * 11 & 255, (x * y) & 255)) for y in range(H)], hsize=12)
rgb = np.array(photo(W, H))
def p555(r, g, b): return struct.pack('<H', ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
def p565(r, g, b): return struct.pack('<H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
bmp('m_16_555.bmp', W, H, 16, [b''.join(p555(*rgb[y, x]) for x in range(W)) for y in range(H)])
bmp('m_16_565.bmp', W, H, 16, [b''.join(p565(*rgb[y, x]) for x in range(W)) for y in range(H)], comp=3, masks=(0xf800, 0x07e0, 0x001f))
bmp('m_16_565_v4.bmp', W, H, 16, [b''.join(p565(*rgb[y, x]) for x in range(W)) for y in range(H)], comp=3, masks=(0xf800, 0x07e0, 0x001f), hsize=108)
bmp('m_32_bf.bmp', W, H, 32, [b''.join(struct.pack('<I', (int(rgb[y, x, 0]) << 8) | (int(rgb[y, x, 1]) << 16) | (int(rgb[y, x, 2]) << 24)) for x in range(W)) for y in range(H)],
    comp=3, masks=(0xff00, 0xff0000, 0xff000000))
np.save(os.path.join(OUT, 'm_32_bf.bmp.npy'), rgb)   # PIL can't read this layout: the reference

def rle8(rows):
    out = bytearray()
    for r in rows[::-1]:                        # bottom-up
        i = 0
        while i < len(r):
            j = i
            while j < len(r) and j - i < 255 and r[j] == r[i]: j += 1
            if j - i >= 3 or len(r) - i < 3:
                out += bytes([j - i, r[i]]); i = j
            else:
                n = min(255, len(r) - i)
                k = i
                while k < i + n and not (k + 2 < len(r) and r[k] == r[k + 1] == r[k + 2]): k += 1
                n = max(3, k - i) if len(r) - i >= 3 else len(r) - i
                n = min(n, len(r) - i)
                if n < 3:
                    out += bytes([1, r[i]]); i += 1; continue
                out += bytes([0, n]) + bytes(r[i:i + n]) + (b'\0' if n & 1 else b'')
                i += n
        out += b'\0\0'
    return bytes(out[:-2]) + b'\0\1'

def rle4(rows):
    out = bytearray()
    for r in rows[::-1]:
        i = 0
        while i < len(r):
            if i + 1 < len(r) and rnd.random() < 0.5:   # a run of 2..8 alternating pairs
                n = min(len(r) - i, rnd.randrange(2, 9))
                a, b = r[i], r[i + 1]
                for k in range(n): r[i + k] = a if k % 2 == 0 else b
                out += bytes([n, (a << 4) | b]); i += n
            else:
                n = min(len(r) - i, rnd.randrange(3, 12))
                if n < 3:
                    out += bytes([n, r[i] << 4 | (r[i + 1] if n > 1 else 0)]);
                    if n > 1: r[i + 1] = r[i + 1]
                    i += n; continue
                lit = pack(r[i:i + n], 4)
                out += bytes([0, n]) + lit + (b'\0' if len(lit) & 1 else b'')
                i += n
        out += b'\0\0'
    return bytes(out[:-2]) + b'\0\1'

runs = [[(x // 5 + y) % 256 if x % 9 else rnd.randrange(256) for x in range(W)] for y in range(H)]
bmp('m_rle8.bmp', W, H, 8, None, P256, comp=1, data=rle8(runs))
r4 = [[rnd.randrange(16) for _ in range(W)] for _ in range(H)]
data4 = rle4(r4)                                # (rle4 rewrites r4 to what it encoded)
bmp('m_rle4.bmp', W, H, 4, None, P16, comp=2, data=data4)
np.save(os.path.join(OUT, 'm_rle4.bmp.col.npy'), np.array(P16, np.uint8)[np.array(r4)])   # PIL drops a pixel of odd absolute runs
# RLE8 with a delta escape: rows 0-1 drawn, then skip 3 right and 2 up, a run, end of picture
d = bytearray()
d += bytes([W, 7, 0, 0])                        # row (bottom) 0: all colour 7
d += bytes([5, 9, 0, 2, 3, 2, 4, 11, 0, 1])     # row 1: 5 x 9, delta (3,2) -> row 3 x 8: 4 x 11, end
bmp('m_rle8_delta.bmp', W, H, 8, None, P256, comp=1, data=bytes(d))
ex = np.zeros((H, W), int)                      # bottom-up rows: file row y = picture row H-1-y
ex[H - 1, :] = 7; ex[H - 2, :5] = 9; ex[H - 4, 8:12] = 11
np.save(os.path.join(OUT, 'm_rle8_delta.bmp.col.npy'), np.array(P256, np.uint8)[ex])   # PIL can't end early

# ---- TGA / PCX / ICO / QOI
import io
for (w, h) in [(1, 1), (7, 5), (33, 17), (640, 480)]:
    for comp in (None, 'tga_rle'):
        tag = '_rle' if comp else ''
        kw = {'compression': comp} if comp else {}
        photo(w, h).save(os.path.join(OUT, 't_%dx%d_24%s.tga' % (w, h, tag)), **kw)
        photo(w, h).convert('RGBA').save(os.path.join(OUT, 't_%dx%d_32%s.tga' % (w, h, tag)), **kw)
        photo(w, h).convert('L').save(os.path.join(OUT, 't_%dx%d_8g%s.tga' % (w, h, tag)), **kw)
        pal_image(w, h, 256).save(os.path.join(OUT, 't_%dx%d_8p%s.tga' % (w, h, tag)), **kw)
    photo(w, h).convert('1').save(os.path.join(OUT, 'p_%dx%d_1.pcx' % (w, h)))
    photo(w, h).convert('L').save(os.path.join(OUT, 'p_%dx%d_8g.pcx' % (w, h)))
    pal_image(w, h, 256).save(os.path.join(OUT, 'p_%dx%d_8p.pcx' % (w, h)))
    if w > 1: photo(w, h).save(os.path.join(OUT, 'p_%dx%d_24.pcx' % (w, h)))   # (PIL writes 1x1 24-bit PCX short)
    photo(w, h).save(os.path.join(OUT, 'q_%dx%d_3.qoi' % (w, h)))
    photo(w, h).convert('RGBA').save(os.path.join(OUT, 'q_%dx%d_4.qoi' % (w, h)))
# QOI with long runs and index hits
flat = Image.new('RGB', (100, 60), (10, 200, 30)); flat.paste((200, 10, 30), (20, 10, 70, 40))
flat.save(os.path.join(OUT, 'q_runs.qoi'))

def tga(name, w, h, itype, bpp, rows, desc=0, cmap=None, cmbits=24, cmfirst=0, idfield=b''):
    """rows: bytes per row, top first; stored bottom-up unless desc bit 5; bit 4 = right to left"""
    hd = struct.pack('<BBBHHBHHHHBB', len(idfield), 1 if cmap else 0, itype, cmfirst, len(cmap or []), cmbits if cmap else 0,
                     0, 0, w, h, bpp, desc)
    cm = b''
    for c in cmap or []:
        if cmbits == 24: cm += bytes([c[2], c[1], c[0]])
        else: cm += struct.pack('<H', ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3))
    bpb = (bpp + 7) // 8
    order = rows if desc & 0x20 else rows[::-1]
    if desc & 0x10: order = [b''.join(r[i:i + bpb] for i in range(len(r) - bpb, -1, -bpb)) for r in order]
    open(os.path.join(OUT, name), 'wb').write(hd + idfield + cm + b''.join(order))

rgbT = np.array(photo(W, H))
def p16(r, g, b): return struct.pack('<H', ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3))
tga('u_16.tga', W, H, 2, 16, [b''.join(p16(*rgbT[y, x]) for x in range(W)) for y in range(H)])
np.save(os.path.join(OUT, 'u_24_top.tga.npy'), rgbT)   # an ID of 10 bytes = first byte 0x0A: PIL takes it for PCX
tga('u_24_top.tga', W, H, 2, 24, [b''.join(bytes([rgbT[y, x, 2], rgbT[y, x, 1], rgbT[y, x, 0]]) for x in range(W)) for y in range(H)], desc=0x20, idfield=b'AmiDT test')
tga('u_24_rtl.tga', W, H, 2, 24, [b''.join(bytes([rgbT[y, x, 2], rgbT[y, x, 1], rgbT[y, x, 0]]) for x in range(W)) for y in range(H)], desc=0x10)
tga('u_cm16.tga', W, H, 1, 8, [bytes(v % 40 for v in r) for r in I256], cmap=[P256[i] for i in range(40)], cmbits=16)
tga('u_cm_first.tga', W, H, 1, 8, [bytes(10 + v % 20 for v in r) for r in I256], cmap=[P256[i] for i in range(20)], cmfirst=10)
# PCX: EGA 4 planes x 1 bit with the header palette
def pcx_planes(name, idx, pal16):
    h_, w_ = len(idx), len(idx[0]); bpl = ((w_ + 15) // 16) * 2
    hd = bytearray(128); hd[0] = 10; hd[1] = 5; hd[2] = 1; hd[3] = 1
    struct.pack_into('<HHHH', hd, 4, 0, 0, w_ - 1, h_ - 1)
    for i, c in enumerate(pal16): hd[16 + i * 3:19 + i * 3] = bytes(c)
    hd[65] = 4; struct.pack_into('<H', hd, 66, bpl)
    data = bytearray()
    for r in idx:
        for p in range(4):
            line = pack([(v >> p) & 1 for v in r], 1); line += b'\0' * (bpl - len(line))
            for b in line:                      # rle: every byte as a 1-run if it looks like a count
                data += bytes([0xC1, b]) if b >= 0xC0 else bytes([b])
    open(os.path.join(OUT, name), 'wb').write(bytes(hd) + bytes(data))
    np.save(os.path.join(OUT, name + '.col.npy'), np.array(pal16, np.uint8)[np.array(idx)])
pcx_planes('v_ega.pcx', I16, P16)
# ICO: several sizes; BMP inside (4/8/32-bit) and PNG inside
src = photo(64, 64)
src.save(os.path.join(OUT, 'i_png.ico'), sizes=[(16, 16), (32, 32), (64, 64)])
src.save(os.path.join(OUT, 'i_bmp32.ico'), sizes=[(16, 16), (48, 48)], bitmap_format='bmp')
for bits, n in ((4, 16), (8, 256)):             # PIL can't write palette icons: by hand
    im = src.resize((32, 32)).quantize(n)
    pal = im.getpalette()[:n * 3] + [0] * (n * 3 - len(im.getpalette()[:n * 3]))
    idx = np.array(im)
    rows = [pack(list(r), bits) for r in idx[::-1]]
    stride = ((32 * bits + 31) // 32) * 4
    xor = b''.join(r + b'\0' * (stride - len(r)) for r in rows)
    andm = b'\0' * (4 * 32)
    dibh = struct.pack('<IiiHHIIiiII', 40, 32, 64, 1, bits, 0, 0, 0, 0, 0, 0)
    palb = b''.join(bytes([pal[i * 3 + 2], pal[i * 3 + 1], pal[i * 3], 0]) for i in range(n))
    img = dibh + palb + xor + andm
    small = struct.pack('<IiiHHIIiiII', 40, 1, 2, 1, 1, 0, 0, 0, 0, 0, 0) + b'\0\0\0\0\xff\xff\xff\0' + b'\0' * 8   # a 1x1 decoy
    ent = lambda w_, h_, bpp_, size, off: struct.pack('<BBBBHHII', w_, h_, 0, 0, 1, bpp_, size, off)
    d = struct.pack('<HHH', 0, 1, 2)
    off1 = 6 + 32; off2 = off1 + len(small)
    d += ent(1, 1, 1, len(small), off1) + ent(32, 32, bits, len(img), off2) + small + img
    open(os.path.join(OUT, 'i_bmp%d.ico' % bits), 'wb').write(d)
    np.save(os.path.join(OUT, 'i_bmp%d.ico.col.npy' % bits), np.array(pal, np.uint8).reshape(-1, 3)[idx])
print(len(os.listdir(OUT)), 'files in', OUT)
