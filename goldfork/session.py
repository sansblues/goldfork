"""A game parked at a fork point, evaluating batches of script continuations.

    with Session('seg.hltas', frame=4700, workdir='runs/x') as s:
        trajs = s.evaluate([suffix_a, suffix_b])   # lists of frame lines

The game plays the script up to `frame` once, then serves batches: every
continuation runs in its own forked copy of the game (exactly equal to a
linear run of prefix + continuation) and returns its per-frame trajectory.
"""
import errno, json, os, re, shutil, signal, subprocess, time
from dataclasses import dataclass

from .hltas import Script

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAY = os.path.join(ROOT, 'play.sh')
HL_ROOT = os.environ.get('HL_ROOT', os.path.expanduser('~/.steam/steam/steamapps/common/Half-Life'))
SAVE = os.path.join(HL_ROOT, 'valve', 'SAVE')
END = '----------|------|------|{ft}|-|-|1|_bxt_fork_exit'


@dataclass
class Frame:
    """Player state after one command frame (one usercmd) of a child."""
    pos: tuple
    vel: tuple
    on_ground: bool
    health: float
    bulk: int        # script bulk index that produced the frame
    ms: int = 0      # the frame's msec (0 for 0 ms frames)


class Trajectory(list):
    """Frames of one run; `end` names the map if the run ended by a level change."""
    end = None


def read_trajectory(path, from_bid=None):
    """Per-command-frame states from a taslogger log: from its start (a fork
    child's log starts at the fork frame), or from the first frame of script
    line `from_bid` (BXT's index of frame lines after 'frames')."""
    with open(path) as f:
        log = json.load(f)
    out = Trajectory()
    if os.path.exists(path + '.end'):
        why, _, map = open(path + '.end').read().strip().partition(' ')
        out.end = map if why == 'changelevel' else f'{why} {map}'   # 'c1a3', 'spawn c1a2b'
    started = from_bid is None
    for pf in log['pf']:
        if not started:
            if not any(cf.get('bid') == from_bid for cf in pf.get('cf', [])):
                continue
            started = True
        # A linear run goes on into the next map; end it where the fork child
        # would have: before the frame that loads it. The engine loads the
        # destination's saved state first.
        # (Loading a .sav is part of the run and doesn't end it.)
        load = next((m for m in pf.get('cmsg', []) if m.startswith('Loading game from ')), None)
        m = load and re.match(r'Loading game from SAVE/(\w+)\.HL1', load)
        if m:
            out.end = m.group(1)
            break
        for cf in pf.get('cf', []):
            post = cf['postpm']
            out.append(Frame(tuple(post['pos']), tuple(post['vel']), bool(post.get('og')),
                             cf.get('hp', 0.0), cf.get('bid', -1), cf.get('ms', 0)))
    return out


class Session:
    def __init__(self, script, frame, workdir, map='c1a0', jobs=None, game='valve'):
        self.script = Script.read(script)
        if not any(l.startswith('seed ') for l in self.script.header):
            print(f'[goldfork] warning: {script} has no seed line; the first map\'s RNG is seeded '
                  'from the clock, so replays in a fresh game may differ', flush=True)
        self.prefix, self.fork_bulk, self.rest = self.script.split_at(frame)
        self.workdir = os.path.abspath(workdir)
        assert '-' not in self.workdir, "Half-Life's command line splits on '-'"
        os.makedirs(self.workdir, exist_ok=True)
        self.map, self.jobs, self.game = map, jobs or os.cpu_count(), game
        self.batches = 0
        ft = self.fork_bulk.fields[3]
        self.end = END.format(ft=ft)

        req, rep = (os.path.join(self.workdir, n) for n in ('req.fifo', 'rep.fifo'))
        for p in (req, rep):
            if os.path.exists(p):
                os.remove(p)
            os.mkfifo(p)
        serve = self.fork_bulk.with_(commands=';'.join(
            c for c in (self.fork_bulk.commands, f'bxt_fork_serve {req} {rep}') if c))
        self.head = self.prefix + [serve.line()]
        parent = os.path.join(self.workdir, 'parent.hltas')
        self.script.write(parent, self.head + [self.end])

        self.out = open(os.path.join(self.workdir, 'game.out'), 'w')
        # Its own process group: the game runs below play.sh, so closing must
        # signal the whole group, not just the shell.
        self.proc = subprocess.Popen([PLAY, parent, '-', map, game], stdout=self.out, stderr=subprocess.STDOUT,
                                     start_new_session=True)
        self.req = self._open_fifo(req)
        self.rep = open(rep)

    def _open_fifo(self, path, timeout=300):
        """Opens the request fifo for writing once the game is listening."""
        deadline = time.time() + timeout
        while True:
            try:
                return os.fdopen(os.open(path, os.O_WRONLY | os.O_NONBLOCK), 'w')
            except OSError as e:
                if e.errno != errno.ENXIO:
                    raise
            if self.proc.poll() is not None:
                raise RuntimeError(f'game exited before reaching the fork point; see {self.out.name}')
            if time.time() > deadline:
                raise TimeoutError('game did not reach the fork point')
            time.sleep(0.05)

    def evaluate(self, suffixes, keep=False):
        """Runs each continuation (a list of frame lines) and returns trajectories.

        Each trajectory starts with the fork frame itself. A continuation that
        failed (bad script, crash, timeout) yields None.
        """
        b = self.batches = self.batches + 1
        base = os.path.join(self.workdir, f'b{b}')
        for i, s in enumerate(suffixes):
            self.script.write(f'{base}c{i}.hltas', self.head + list(s) + [self.end])
        self.req.write(f'batch {len(suffixes)} {self.jobs} {base}c%d.hltas {base}c%d.json\n')
        self.req.flush()
        reply = self.rep.readline()
        if not reply.startswith('done'):
            raise RuntimeError(f'fork server: {reply!r}')
        self.last = dict(zip(('started', 'failed', 'seconds'), map(float, reply.split()[1:])))
        shutil.rmtree(os.path.join(SAVE, '.fork'), ignore_errors=True)   # children's private saves
        out = []
        for i in range(len(suffixes)):
            log = f'{base}c{i}.json'
            try:
                out.append(read_trajectory(log))
            except (OSError, ValueError, KeyError):
                out.append(None)
                # Keep failed candidates (crash, timeout) for a post-mortem.
                os.makedirs(os.path.join(self.workdir, 'failed'), exist_ok=True)
                os.replace(f'{base}c{i}.hltas', os.path.join(self.workdir, 'failed', f'b{b}c{i}.hltas'))
            if not keep:
                for p in (log, log + '.end', f'{base}c{i}.hltas'):
                    if os.path.exists(p):
                        os.remove(p)
        return out

    def close(self):
        if self.proc.poll() is None:
            try:
                self.req.write('quit\n'); self.req.flush()
            except BrokenPipeError:
                pass
            try:
                self.proc.wait(timeout=60)
            except subprocess.TimeoutExpired:
                pass
        try:
            os.killpg(self.proc.pid, signal.SIGKILL)       # anything left of the game
        except ProcessLookupError:
            pass
        self.out.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def verify(session, continuation, objective, name='verify'):
    """Plays prefix + continuation linearly in a fresh game (no forking) and
    returns (objective score, trajectory from the fork frame on, script path).

    This is the independent check for any result a search reports.
    """
    path = os.path.join(session.workdir, f'{name}.hltas')
    log = os.path.join(session.workdir, f'{name}.json')
    plain = session.fork_bulk.line()          # the fork frame without the server
    session.script.write(path, session.prefix + [plain] + list(continuation) + [session.end])
    subprocess.run([PLAY, path, log, session.map, session.game],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fork_bid = sum(1 for l in session.prefix if l.strip() and not l.lstrip().startswith('//'))
    tail = read_trajectory(log, from_bid=fork_bid)
    return objective(tail), tail, path

