#!/bin/sh
# fuzz.sh - mutate the test files (every format) (truncate, flip bytes) and decode each under
# AddressSanitizer; any memory error, crash or hang is printed.  Run from WSL in the repo.
gcc -g -fsanitize=address,undefined -fno-sanitize=alignment -O1 -o /tmp/hp_fuzz test/host_png.c src/core/*.c || exit 1
python3 - "$@" <<'PY'
import glob, os, random, subprocess, sys
r = random.Random(7)
files = sorted(sum((glob.glob('test/corpus/*.' + e) for e in ('gif', 'bmp', 'tga', 'pcx', 'ico', 'qoi')), []) + glob.glob('test/pngsuite/b*.png'))
n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
bad = runs = 0
for f in files:
    d = open(f, 'rb').read()
    for k in range(n):
        m = bytearray(d)
        how = r.randrange(3)
        if how == 0: m = m[:r.randrange(1, len(m))]
        else:
            for _ in range(r.randrange(1, 8)): m[r.randrange(len(m))] = r.randrange(256)
        if how == 2: m = m[:r.randrange(1, len(m))]
        fz = '/tmp/fz' + os.path.splitext(f)[1]          # the extension picks TGA
        open(fz, 'wb').write(m)
        runs += 1
        try:
            p = subprocess.run(['/tmp/hp_fuzz', fz, '/tmp/fz.idx'], capture_output=True, text=True, timeout=20)
            if p.returncode not in range(0, 8) or 'ERROR' in p.stderr or 'runtime error' in p.stderr:
                bad += 1; open('/tmp/fz_bad_%d.bin' % bad, 'wb').write(m)
                print('BAD', f, k, p.returncode, p.stderr.strip().splitlines()[:3])
        except subprocess.TimeoutExpired:
            bad += 1; open('/tmp/fz_bad_%d.bin' % bad, 'wb').write(m); print('HANG', f, k)
print(runs, 'runs,', bad, 'bad')
PY
