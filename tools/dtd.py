"""dtd.py - read and write datatype descriptors (DEVS:DataTypes/<name>, IFF FORM DTYP).

    python tools/dtd.py dump FILE...                 show what a descriptor says
    (import) make(name, base, pattern, mask, gid, id, flags=0, pri=0, ver=None) -> bytes

DTHD = struct DataTypeHeader: Name, BaseName, Pattern, Mask (offsets from the chunk start),
GroupID, ID, MaskLen (WORDs; -1 = any byte), Pad, Flags (DTF_BINARY 0 / ASCII 1 / IFF 2 / MISC 3,
DTF_CASE 0x10), Priority; the strings and the mask follow the 32-byte header.
"""
import struct, sys

def chunks(d):
    assert d[:4] == b'FORM' and d[8:12] == b'DTYP', 'not a FORM DTYP'
    i, end = 12, 8 + struct.unpack('>I', d[4:8])[0]
    while i < end:
        cid, n = d[i:i + 4], struct.unpack('>I', d[i + 4:i + 8])[0]
        yield cid.decode('latin-1'), d[i + 8:i + 8 + n]
        i += 8 + n + (n & 1)

def cstr(b, off):
    return b[off:b.index(b'\0', off)].decode('latin-1')

def dump(path):
    d = open(path, 'rb').read()
    print('==', path, len(d), 'bytes')
    for cid, b in chunks(d):
        if cid == 'DTHD':
            nm, bn, pt, mk, gid, idv, ml, pad, fl, pri = struct.unpack('>IIIIIIhhHH', b[:32])
            mask = struct.unpack('>%dh' % ml, b[mk:mk + 2 * ml]) if ml > 0 else ()
            print('  DTHD name=%r base=%r pattern=%r group=%r id=%r flags=%#x pri=%d' % (
                cstr(b, nm), cstr(b, bn), cstr(b, pt), gid.to_bytes(4, 'big'), idv.to_bytes(4, 'big'), fl, pri))
            print('       mask', ' '.join('??' if m < 0 else '%02x' % m for m in mask))
        else:
            print('  %s %r' % (cid, b[:60]))

def make(name, base, pattern, mask, gid, idv, flags=0, pri=0, ver=None):
    strs = b''
    hdr_len = 32
    mask_off = hdr_len
    mbytes = b''.join(struct.pack('>h', m) for m in mask)
    off = mask_off + len(mbytes)
    offs = []
    for s in (name, base, pattern):
        offs.append(off)
        sb = s.encode('latin-1') + b'\0'
        strs += sb; off += len(sb)
    body = struct.pack('>IIIIIIhhHH', offs[0], offs[1], offs[2], mask_off if mask else 0, gid, idv, len(mask), 0, flags, pri) + mbytes + strs
    if len(body) & 1: body += b'\0'
    if len(body) % 4: body += b'\0' * (4 - len(body) % 4)
    out = b''
    if ver:
        v = ver.encode('latin-1') + b'\0'
        if len(v) & 1: v += b'\0'
        out += b'FVER' + struct.pack('>I', len(v)) + v
    nb = name.encode('latin-1') + b'\0'
    if len(nb) & 1: nb += b'\0'
    out += b'NAME' + struct.pack('>I', len(nb)) + nb
    out += b'DTHD' + struct.pack('>I', len(body)) + body
    return b'FORM' + struct.pack('>I', len(out) + 4) + b'DTYP' + out

if __name__ == '__main__' and sys.argv[1] == 'dump':
    for f in sys.argv[2:]: dump(f)
