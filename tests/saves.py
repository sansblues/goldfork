# Branches save and load privately: each saves under the same name at a
# different place, walks away and loads back. Each must return exactly to its
# own save, repeat runs must be identical, and the game's SAVE must not change.
import os
os.environ['BXT_FORK_ALLOW_LOADS'] = '1'
from common import seeded_script, workdir
from goldfork.session import Session, SAVE

W = '----------|f-----|------|0.01|-14.3|25|{n}'
B = '----------|---b--|------|0.01|-14.3|25|{n}'
C = '----------|------|------|0.01|-14.3|25|1|{cmd}'


def child(i):
    return [W.format(n=5 + 5 * i), C.format(cmd='save t'), B.format(n=30),
            C.format(cmd='load t'), '----------|------|------|0.01|-14.3|25|60']


ok = True
with workdir() as w, Session(seeded_script(w), frame=2000, workdir=w, map='c1a0', jobs=8) as s:
    before = sorted(f for f in os.listdir(SAVE) if f != '.fork')
    runs = [s.evaluate([child(i) for i in range(8)]) for _ in range(2)]
    after = sorted(f for f in os.listdir(SAVE) if f != '.fork')
for i, (a, b) in enumerate(zip(*runs)):
    saved = a and next(f for f in a if f.bulk == a[-1].bulk - 3)
    loaded = a and next(f for f in a if f.bulk == a[-1].bulk)
    good = bool(a) and (saved.pos, saved.vel) == (loaded.pos, loaded.vel) and b == a
    ok &= good
    print(f'{"PASS" if good else "FAIL"}  child {i}')
clean = after == before and not os.path.exists(os.path.join(SAVE, '.fork'))
print(f'{"PASS" if clean else "FAIL"}  SAVE unchanged')
print('PASS' if ok and clean else 'FAIL')
