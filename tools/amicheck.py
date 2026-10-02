"""amicheck.py - decode files on an Amiga with an AmiDT class and compare its picture byte for byte
with the PC harness (test/host_png) - then time it against the system's class.

    python tools/amicheck.py HOST CLASSFILE FILE...          (HOST 127.0.0.1 = the bench)

The class is copied to RAM:dt/c/amidt/, the files to RAM:dt/; dtbench and the runs go through
dtrun.py (one job at a time, no Avail FLUSH).  CLASSFILE must be a benchmark build (amidt*.datatype).
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
host, cls, files = sys.argv[1], sys.argv[2], sys.argv[3:]
if not os.path.exists(cls): sys.exit('no class file %s (make bench?)' % cls)
NH = [sys.executable, r'C:\projects\NetHarness\host\nhctl.py', '--host', host, '--port', os.environ.get('NHPORT', '7800')]
def nh(*a): return subprocess.run(NH + list(a), capture_output=True, text=True, encoding='latin-1').stdout
def wsl(p):
    p = os.path.abspath(p).replace('\\', '/'); return '/mnt/' + p[0].lower() + p[2:]

nh('EXEC', 'MakeDir RAM:dt RAM:dt/c RAM:dt/c/amidt')
nh('PUTFILE', os.path.join(ROOT, 'out', 'dtbench'), 'RAM:dt/dtbench')
nh('PUTFILE', cls, 'RAM:dt/c/amidt/' + os.path.basename(cls))
ok = bad = 0
jobs = []
for f in files:
    b = os.path.basename(f)
    nh('PUTFILE', f, 'RAM:dt/' + b)
    ref = os.path.join(os.environ.get('TEMP', '/tmp'), 'ref_' + b + '.idx')
    subprocess.run(['wsl', '-e', wsl(os.path.join(ROOT, 'test', 'host_png')), wsl(f), wsl(ref)], capture_output=True)
    r = subprocess.run([sys.executable, os.path.join(HERE, 'dtrun.py'), host,
                        'check|%s REPEAT=1 LAYOUT CLASS=RAM:dt/c/amidt/%s DUMP=RAM:dt/d.idx' % (b, os.path.basename(cls))],
                       capture_output=True, text=True)
    if r.returncode: print(r.stdout); print('STOPPED: the Amiga no longer answers'); sys.exit(1)
    got = os.path.join(os.environ.get('TEMP', '/tmp'), 'ami_' + b + '.idx')
    if os.path.exists(got): os.remove(got)
    nh('GETFILE', 'RAM:dt/d.idx', got)
    same = os.path.exists(got) and open(got, 'rb').read() == open(ref, 'rb').read()
    ok += same; bad += not same
    print('%-24s %s' % (b, 'IDENTICAL' if same else 'DIFFERENT'))
    jobs += ['AmiDT %s|%s REPEAT=3 LAYOUT CLASS=RAM:dt/c/amidt/%s' % (b, b, os.path.basename(cls)), 'OS %s|%s REPEAT=1 LAYOUT' % (b, b)]
print('%d identical, %d different' % (ok, bad))
subprocess.run([sys.executable, os.path.join(HERE, 'dtrun.py'), host] + jobs)
