"""Minimal .hltas handling: split a script at a movement frame, write scripts.

Lines are kept as text. A movement bulk is a line with at least 7 '|'-separated
fields whose 7th is the repeat count; everything else is passed through.
"""
from dataclasses import dataclass, field


@dataclass
class Bulk:
    """A movement frame bulk: fields[0..6] plus optional commands."""
    fields: list
    commands: str = ''

    FLAGS, MOVE, ACTIONS, FRAMETIME, YAW, PITCH, REPEATS = range(7)

    @staticmethod
    def parse(line):
        if line.lstrip().startswith('//'):     # a comment, not a frame
            return None
        f = line.split('|')
        if len(f) < 7 or not f[6].strip().isdigit():
            return None
        return Bulk(f[:7], '|'.join(f[7:]))

    @property
    def repeats(self):
        return int(self.fields[6])

    def with_(self, **kw):
        """A copy with fields replaced by name, e.g. with_(yaw='90', repeats=3)."""
        names = ['flags', 'move', 'actions', 'frametime', 'yaw', 'pitch', 'repeats']
        f = list(self.fields)
        commands = kw.pop('commands', self.commands)
        for k, v in kw.items():
            f[names.index(k)] = str(v)
        return Bulk(f, commands)

    def line(self):
        return '|'.join(self.fields + ([self.commands] if self.commands else []))


def is_movement(line):
    return Bulk.parse(line) is not None


@dataclass
class Script:
    header: list            # lines up to and including 'frames'
    lines: list = field(default_factory=list)   # frame lines (text)

    @staticmethod
    def read(path):
        text = open(path).read().split('\n')
        start = text.index('frames') + 1
        return Script(text[:start], [l for l in text[start:] if l.strip()])

    def write(self, path, lines=None):
        with open(path, 'w') as f:
            f.write('\n'.join(self.header + (self.lines if lines is None else lines)) + '\n')

    def split_at(self, frame):
        """Splits so that movement frame `frame` (0-based) starts a new bulk.

        Returns (prefix, fork_bulk, rest): fork_bulk is a one-frame copy of the
        bulk holding that frame (it carries the fork command), rest is what
        follows it. Commands run on every frame of a bulk, so splitting a bulk
        into parts with the same fields leaves the run unchanged.
        """
        seen = 0
        for i, l in enumerate(self.lines):
            b = Bulk.parse(l)
            if not b:
                continue
            if seen + b.repeats > frame:
                k = frame - seen                     # frames of b before the split
                prefix = self.lines[:i] + ([b.with_(repeats=k).line()] if k else [])
                fork = b.with_(repeats=1)
                left = b.repeats - k - 1
                rest = ([b.with_(repeats=left).line()] if left else []) + self.lines[i + 1:]
                return prefix, fork, rest
            seen += b.repeats
        raise ValueError(f'script has only {seen} movement frames')
