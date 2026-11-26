"""Search over script continuations, evaluated exactly by forking the game.

A candidate is a continuation: the frame lines after the fork point. An
objective maps a trajectory to a score (lower is better) or None if invalid.
"""
import math, random
from dataclasses import dataclass

from .hltas import Bulk


# ---------------------------------------------------------------- objectives

def reach_box(lo, hi):
    """Fewest frames until the player origin is inside the box [lo, hi].

    Unreached runs are invalid for ranking against reached ones but still
    ordered among themselves by how close they got, so search can climb
    towards the box from a seed that misses it.
    """
    def score(traj):
        best = math.inf
        for i, f in enumerate(traj):
            d2 = sum(max(l - p, 0, p - h) ** 2 for p, l, h in zip(f.pos, lo, hi))
            if d2 == 0:
                speed = math.hypot(f.vel[0], f.vel[1])
                return (0, i, -speed)          # reached: frames, then speed
            best = min(best, d2)
        return (1, math.sqrt(best), 0)          # missed: closest approach
    return score


def reach_level(map, lo, hi):
    """Least game time until a level change to `map`, staying alive.

    Score (0, ms) once it is reached. Runs that miss it are ordered by how
    close the player origin got to the box [lo, hi] (the trigger, grown by
    the player hull), and runs where the player dies rank last.
    """
    def score(traj):
        if any(f.health <= 0 for f in traj):
            return (2, 0)
        if traj.end == map:
            return (0, sum(f.ms for f in traj[1:]))
        best = min(sum(max(l - p, 0, p - h) ** 2 for p, l, h in zip(f.pos, lo, hi)) for f in traj)
        return (1, math.sqrt(best))
    return score


def max_distance(seconds):
    """Largest horizontal distance from the fork position reached within
    `seconds` of game time (0 ms frames are free). Score: negative units."""
    def score(traj):
        o, t, best = traj[0].pos, 0, 0.0
        for f in traj[1:]:
            t += f.ms
            if t > seconds * 1000:
                break
            best = max(best, math.hypot(f.pos[0] - o[0], f.pos[1] - o[1]))
        return (-best,)
    return score


def speed_at(seconds):
    """Horizontal speed at `seconds` of game time after the fork frame (the
    last frame not past it; 0 ms frames are free). Score: negative speed."""
    def score(traj):
        t, last = 0, traj[0]
        for f in traj[1:]:
            t += f.ms
            if t > seconds * 1000:
                break
            last = f
        return (-math.hypot(last.vel[0], last.vel[1]), 'speed')
    return score


def peak_speed(seconds):
    """Highest horizontal speed reached within `seconds` of game time after the
    fork frame (0 ms frames are free). Score: negative speed."""
    def score(traj):
        t, best = 0, 0.0
        for f in traj[1:]:
            t += f.ms
            if t > seconds * 1000:
                break
            best = max(best, math.hypot(f.vel[0], f.vel[1]))
        return (-best, 'speed')
    return score


def describe(score):
    if score is not None and len(score) == 2 and score[1] == 'speed':
        return f'{-score[0]:.1f} units/s'

    if score is not None and len(score) == 1:
        return f'{-score[0]:.1f} units'

    if score is None:
        return 'invalid'
    if score[0] == 2:
        return 'dead'
    if score[0] == 0:
        return f'reached in {score[1]} ms' if len(score) == 2 else f'reached in {score[1]} frames (speed {-score[2]:.1f})'
    return f'missed by {score[1]:.1f} units'


# ---------------------------------------------------------------- mutations

ACTIONS = 'jdu12r'   # field 2 positions: jump, duck, use, attack1, attack2, reload
MOVES = 'flrbud'     # field 1 positions: forward, left, right, back, up, down


def toggle(field, letters, k):
    f = list(field.ljust(6, '-'))
    f[k] = '-' if f[k] != '-' else letters[k]
    return ''.join(f)


def movement_indices(lines, horizon):
    """Indices of movement bulks that start within the first `horizon` frames."""
    out, seen = [], 0
    for i, l in enumerate(lines):
        b = Bulk.parse(l)
        if b:
            if seen >= horizon:
                break
            out.append(i)
            seen += b.repeats
    return out


def edit_span(lines, start, end, edit):
    """Applies edit(bulk) -> bulk to movement frames [start, end) of a
    continuation, splitting the bulks at both ends."""
    out, f = [], 0
    for l in lines:
        b = Bulk.parse(l)
        if not b:
            out.append(l)
            continue
        a, z = f, f + b.repeats
        cuts = sorted({a, z, min(max(start, a), z), min(max(end, a), z)})
        for lo, hi in zip(cuts, cuts[1:]):
            piece = b.with_(repeats=hi - lo)
            out.append((edit(piece) if start <= lo and hi <= end else piece).line())
        f = z
    return out


def mutate(lines, rng, horizon, frametimes=(), actions=ACTIONS):
    """One random edit of a continuation; returns a new list of lines."""
    lines = list(lines)
    idx = movement_indices(lines, horizon)
    if not idx:
        return lines
    i = rng.choice(idx)
    b = Bulk.parse(lines[i])
    op = rng.random()

    if op < 0.1:
        # Hold or release an action or movement key over a long span: some
        # tricks (ducking through a doorway) need one change over hundreds of
        # frames, which short edits can't build up step by step.
        start = rng.randrange(horizon)
        end = start + int(8 * 50 ** rng.random())        # 8 to 400 frames, log-uniform
        if rng.random() < 0.5:
            k = rng.choice([k for k, a in enumerate(ACTIONS) if a in actions])
            hold = rng.random() < 0.5
            def edit(p):
                f = list(p.fields[Bulk.ACTIONS].ljust(6, '-'))
                f[k] = ACTIONS[k] if hold else '-'
                return p.with_(actions=''.join(f))
        else:
            k = rng.randrange(4)
            hold = rng.random() < 0.5
            def edit(p):
                f = list(p.fields[Bulk.MOVE].ljust(6, '-'))
                f[k] = MOVES[k] if hold else '-'
                return p.with_(move=''.join(f))
        return edit_span(lines, start, end, edit)

    if op < 0.15:
        # Rotate the yaw of a run of consecutive bulks together: strafing
        # paths respond to coordinated turns, not to one bulk at a time.
        d = rng.gauss(0, 2.0)
        k = idx.index(i)
        for j in idx[k:k + rng.randint(2, 12)]:
            c = Bulk.parse(lines[j])
            if c.fields[Bulk.YAW].strip() not in ('-', ''):
                lines[j] = c.with_(yaw=f'{(float(c.fields[Bulk.YAW]) + d) % 360:.6g}').line()

    elif op < 0.5 and b.fields[Bulk.YAW].strip() not in ('-', ''):
        sigma = 0.5 if rng.random() < 0.6 else 5.0
        lines[i] = b.with_(yaw=f'{(float(b.fields[Bulk.YAW]) + rng.gauss(0, sigma)) % 360:.6g}').line()

    elif op < 0.75:
        # Move frames to or from a neighbouring bulk with the same frame time.
        j = i + rng.choice((-1, 1))
        c = Bulk.parse(lines[j]) if 0 <= j < len(lines) else None
        if c and c.fields[Bulk.FRAMETIME] == b.fields[Bulk.FRAMETIME]:
            k = rng.randint(1, 10)
            if b.repeats > k:
                lines[i] = b.with_(repeats=b.repeats - k).line()
                lines[j] = c.with_(repeats=c.repeats + k).line()

    else:
        # Split off a short piece and change one or two of its attributes:
        # an action, a movement key, the frame time, the view.
        if b.repeats < 2:
            return lines
        n = rng.randint(1, min(8, b.repeats - 1))
        at = rng.randint(0, b.repeats - n)
        piece = b
        for _ in range(rng.choice((1, 1, 2))):
            r = rng.random()
            if r < 0.35:
                allowed = [k for k, a in enumerate(ACTIONS) if a in actions]
                if allowed:
                    piece = piece.with_(actions=toggle(piece.fields[Bulk.ACTIONS], ACTIONS, rng.choice(allowed)))
            elif r < 0.6:
                piece = piece.with_(move=toggle(piece.fields[Bulk.MOVE], MOVES, rng.randrange(4)))
            elif r < 0.8 and frametimes:
                piece = piece.with_(frametime=rng.choice(frametimes))
            elif piece.fields[Bulk.YAW].strip() not in ('-', ''):
                piece = piece.with_(yaw=f'{(float(piece.fields[Bulk.YAW]) + rng.gauss(0, 30)) % 360:.6g}')
        parts = ([b.with_(repeats=at)] if at else []) + [piece.with_(repeats=n)] + \
                ([b.with_(repeats=b.repeats - at - n)] if b.repeats - at - n else [])
        lines[i:i + 1] = [p.line() for p in parts]
    return lines


# ---------------------------------------------------------------- search

@dataclass
class Result:
    lines: list
    score: tuple
    traj: list


def hill_climb(session, seed, objective, horizon, generations=50, batch=32,
               edits=(1, 3), frametimes=(), rng=None, log=print):
    """Parallel (1+batch) hill climbing with multi-edit mutations.

    Each generation evaluates `batch` mutants of the incumbent in one fork
    batch and keeps the best if it is strictly better.
    """
    rng = rng or random.Random(0)
    [traj] = session.evaluate([seed])
    best = Result(seed, objective(traj), traj)
    log(f'seed: {describe(best.score)}')
    for g in range(generations):
        cands = []
        for _ in range(batch):
            c = best.lines
            for _ in range(rng.randint(*edits)):
                c = mutate(c, rng, horizon, frametimes)
            cands.append(c)
        trajs = session.evaluate(cands)
        scored = [(objective(t), c, t) for c, t in zip(cands, trajs) if t is not None]
        scored = [s for s in scored if s[0] is not None]
        if scored:
            s, c, t = min(scored, key=lambda x: x[0])
            if s < best.score:
                best = Result(c, s, t)
                log(f'gen {g + 1}: {describe(s)}')
    return best


# ---------------------------------------------------------------- quality-diversity

def behaviour(traj, seconds=1.0, cell=48.0):
    """Descriptor for MAP-Elites: (peak horizontal speed, octave bins) and the
    final horizontal position (cell-sized grid), both within `seconds` of game time."""
    t, peak, last = 0, 0.0, traj[0]
    for f in traj[1:]:
        t += f.ms
        if t > seconds * 1000:
            break
        peak = max(peak, math.hypot(f.vel[0], f.vel[1]))
        last = f
    speed_bin = int(math.log2(peak / 100)) + 1 if peak >= 100 else 0
    return (speed_bin, int(last.pos[0] // cell), int(last.pos[1] // cell))


def map_elites(session, seed, objective, horizon, generations=100, batch=48,
               edits=(1, 3), frametimes=(), descriptor=behaviour, rng=None, log=print,
               actions=ACTIONS):
    """MAP-Elites: keep the best candidate per behaviour cell and breed from
    random cells, so stepping stones that don't yet pay off survive.
    Returns (best overall Result, archive)."""
    rng = rng or random.Random(0)
    [traj] = session.evaluate([seed])
    archive = {descriptor(traj): Result(seed, objective(traj), traj)}
    best = archive[descriptor(traj)]
    log(f'seed: {describe(best.score)}')
    for g in range(generations):
        parents = rng.choices(list(archive.values()), k=batch)
        cands = []
        for p in parents:
            c = p.lines
            for _ in range(rng.randint(*edits)):
                c = mutate(c, rng, horizon, frametimes, actions)
            cands.append(c)
        for c, t in zip(cands, session.evaluate(cands)):
            if t is None:
                continue
            s = objective(t)
            if s is None:
                continue
            d = descriptor(t)
            if d not in archive or s < archive[d].score:
                archive[d] = Result(c, s, t)
            if s < best.score:
                best = archive[d]
                log(f'gen {g + 1}: {describe(s)} (cells {len(archive)}, peak-speed bin {d[0]})')
    return best, archive
