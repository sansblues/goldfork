# A game parked for minutes still branches identically: the frozen parent must
# not see real time pass, or the engine drops the local client after about
# 5 minutes.
import sys, time
from common import seeded_script, workdir
from goldfork.session import Session

wait = float(sys.argv[1]) if len(sys.argv) > 1 else 420
cont = ['----------|f-----|------|0.01|-14.3|25|200']
with workdir() as w, Session(seeded_script(w), frame=2000, workdir=w, map='c1a0', jobs=1) as s:
    [a] = s.evaluate([cont])
    time.sleep(wait)
    [b] = s.evaluate([cont])
print('PASS' if a and len(a) > 1 and a == b else 'FAIL')
