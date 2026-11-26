# The headless game replays BXT's own Linux test TAS bit for bit: every frame's
# position and velocity, as float32, equal BXT's recording (94,850 frames).
import json, os, struct, subprocess, sys
from common import ROOT, TEST, workdir

with workdir() as w:
    log = os.path.join(w, 'log.json')
    subprocess.run([os.path.join(ROOT, 'play.sh'), os.path.join(TEST, 'script.hltas'), log, 'c1a0', 'valve'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(log):
        sys.exit('FAIL  the game wrote no log')
    frames = [c['postpm'] for p in json.load(open(log))['pf'] for c in p['cf']]
ours = b''.join(struct.pack('6f', *f['pos'], *f['vel']) for f in frames)
theirs = open(os.path.join(TEST, 'gt'), 'rb').read()
if ours == theirs:
    print(f'PASS  bit-identical: {len(frames)} frames')
else:
    n = next((i for i in range(0, min(len(ours), len(theirs)), 24) if ours[i:i + 24] != theirs[i:i + 24]), None)
    print(f'FAIL  first difference at frame {n // 24}' if n is not None else f'FAIL  {len(ours) // 24} frames, expected {len(theirs) // 24}')
