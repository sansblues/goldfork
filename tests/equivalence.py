# Every branch equals a linear replay of prefix + continuation, including
# continuations whose first bulk changes the frame time, and 0 ms frames.
from common import seeded_script, workdir
from goldfork.session import Session, verify
from goldfork.search import speed_at

conts = []
for ft in ('0.001', '0.004', '0.01', '0.0000000001'):
    conts.append([f'----------|f-----|------|{ft}|-14.3|25|3', '----------|------|------|0.01|-14.3|25|17'])
    conts.append([f'----------|------|-du---|{ft}|-14.3|25|1', '----------|f-----|------|0.002|-14.3|25|19'])
conts.append(['s03-------|------|------|0.01|90|-|20'])
ok = True
with workdir() as w, Session(seeded_script(w), frame=2000, workdir=w, map='c1a0') as s:
    for i, (c, t) in enumerate(zip(conts, s.evaluate(conts))):
        _, linear, _ = verify(s, c, speed_at(1.0), name=f'linear{i}')
        same = bool(t) and len(t) > 1 and t == linear
        ok &= same
        print(f'{"PASS" if same else "FAIL"}  {c[0]}  ({len(t or [])} frames)')
print('PASS' if ok else 'FAIL')
