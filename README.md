# goldfork

Try many endings of a Half-Life TAS from the exact same moment, without replaying the script each time.

```
            played once          frame 4700
run.hltas ───────────────────────────●───── ending A
                                     ├───── ending B
                                     └───── ending C
```

goldfork plays your script once, up to your frame, then forks the paused game for each ending. The results are identical to full replays.

goldfork was **27 times faster** than the bxt-rs optimizer on a 51s script, because it never replays the start.

## Setup

> [!WARNING]
> Use a folder path without `-`, because Half-Life cannot read it.

goldfork requires Linux with Python 3, `bwrap`, `curl` and `git`, and Half-Life on Steam's `steam_legacy` branch.

```sh
git clone https://github.com/sansblues/goldfork.git
cd goldfork
./setup.sh                  # downloads about 3 GB
export HL_ROOT=~/.steam/steam/steamapps/common/Half-Life
python3 tests/bitexact.py   # shows: PASS  bit-identical: 94850 frames
```

## Example

```python
from goldfork.session import Session

forward = ['----------|f-----|------|0.01|90|0|100']   # hold forward for 100 frames
jump    = ['----------|------|j-----|0.01|90|0|100']   # hold jump for 100 frames

with Session('run.hltas', frame=4700, workdir='work', map='c1a0') as s:
    a, b = s.evaluate([forward, jump])
```

`a` and `b` contain the player state on each frame: `pos`, `vel`, `on_ground` and `health`.

## License

MIT
