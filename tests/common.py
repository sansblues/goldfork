"""Shared input for the tests: BXT's own Linux test TAS (c1a0), with a fixed
RNG seed so that every run of it is reproducible."""
import os, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
TEST = os.path.join(ROOT, 'deps', 'BunnymodXT', 'tests', 'tas', '100aa_am_linux')


def workdir():
    """A temporary folder under deps/: Half-Life's command line can't take paths with '-'."""
    return tempfile.TemporaryDirectory(dir=os.path.join(ROOT, 'deps'))


def seeded_script(workdir):
    """A copy of the test script with a seed line; returns its path."""
    os.makedirs(workdir, exist_ok=True)
    lines = open(os.path.join(TEST, 'script.hltas')).read().split('\n')
    lines.insert(1, 'seed 1 1')
    path = os.path.join(workdir, 'seeded.hltas')
    open(path, 'w').write('\n'.join(lines))
    return path
