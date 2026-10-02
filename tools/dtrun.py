"""dtrun.py - run dtbench on a real Amiga over NetHarness, one job at a time, and stop at the first
job after which the Amiga no longer answers (so a crashing datatype is named, not guessed).

    python tools/dtrun.py HOST "label|dtbench args" ...

Each job runs from a script (Run + a done-file) so it can outlast NetHarness's EXEC time limit; the
files must already be in RAM:dt (dtbench, the test files, classes under RAM:dt/c/...).
--flush runs `Avail FLUSH` before each job (expunges unused classes).  Off by default: suspected of the
A1200 crashes (it expunges every unused library/device - a314bsd, RTG, AmiSSL...); give test builds unique
names instead (amidtpN.datatype) so a resident copy can't be picked up.
"""
import os, subprocess, sys, time

FLUSH = '--flush' in sys.argv
if FLUSH: sys.argv.remove('--flush')

NH = [sys.executable, r'C:\projects\NetHarness\host\nhctl.py', '--host', sys.argv[1], '--port', os.environ.get('NHPORT', '7800')]
def nh(*a, t=60):
    return subprocess.run(NH + ['--timeout', str(t)] + list(a), capture_output=True, text=True, encoding='latin-1').stdout

for job in sys.argv[2:]:
    label, args = job.split('|', 1)
    script = 'Stack 20000\nFailAt 21\nCD RAM:dt\n%sRAM:dt/dtbench %s >RAM:dt/one.txt\nEcho done >RAM:dt/done\n' % ('Avail FLUSH >NIL:\n' if FLUSH else '', args)
    open('dtrun.tmp', 'w', newline='\n').write(script)
    nh('PUTFILE', 'dtrun.tmp', 'RAM:dt/one.run')
    nh('EXEC', 'Delete RAM:dt/done QUIET')
    nh('EXEC', 'Run >NIL: Execute RAM:dt/one.run')
    t0 = time.time()
    while time.time() - t0 < 300:
        time.sleep(4)
        if 'done ' in nh('EXEC', 'List RAM:dt/done', t=20):
            break
    else:
        alive = 'OK' in nh('PING', t=20)
        print('%-24s %s' % (label, 'still running after 5 min' if alive else '** THE AMIGA STOPPED ANSWERING **'), flush=True)
        if not alive: sys.exit(1)
        continue
    out = [l for l in nh('EXEC', 'Type RAM:dt/one.txt').splitlines() if l.strip() and not l.startswith('rc=')]
    for l in out: print('%-24s %s' % (label, l.rstrip()[:118]), flush=True)
