#!/bin/bash
# Run Half-Life headless: steam_legacy hl_linux + patched BXT, the null
# renderer and the offline Steam stub, on glibc 2.35, inside Steam's runtime.
# Arguments go to hl_linux.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$FORK_INNER" ]; then
	exec env FORK_INNER=1 "$ROOT/deps/steam-runtime/run.sh" "$0" "$@"
fi
HL_ROOT="${HL_ROOT:-$HOME/.steam/steam/steamapps/common/Half-Life}"
H="$ROOT/host/build"
G="$ROOT/deps/glibc"
cd "$HL_ROOT" || exit 1
export LOCPATH="$ROOT/deps/locale"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy

# The game's browser component (GTK) needs an X display. Each game gets a
# private Xvfb: forked children share their parent's display socket, and a
# desktop compositor must never see that.
unset WAYLAND_DISPLAY
n=90
while grep -q "@/tmp/.X11-unix/X$n\$" /proc/net/unix; do n=$((n + 1)); done
"$ROOT/host/ubuntu.sh" Xvfb ":$n" -nolisten tcp -terminate -screen 0 640x480x24 >/dev/null 2>&1 &
XVFB=$!
# $$ becomes the game below; stop Xvfb once it is gone, even if the game
# never connected (-terminate only fires after a first client).
( while kill -0 $$ 2>/dev/null; do sleep 2; done; kill $XVFB 2>/dev/null ) >/dev/null 2>&1 &
for i in $(seq 200); do grep -q "@/tmp/.X11-unix/X$n\$" /proc/net/unix && break; sleep 0.05; done
export DISPLAY=":$n"

# Only the runtime's own libraries: host ones may need a newer glibc.
RTLIBS=$(tr ':' '\n' <<<"$LD_LIBRARY_PATH" | grep "^${STEAM_RUNTIME:-/nonexistent}/" | grep -v pinned_libs | paste -sd:)
LIBS="$G/lib/i386-linux-gnu:$G/usr/lib/i386-linux-gnu:$H/steam:$H/gl:$H/sdl:$HL_ROOT:$RTLIBS"
# The offline libsteam_api is preloaded, so it satisfies every module's dependency.
PRELOAD="$H/steam/libsteam_api.so $H/libgoldfork.so $ROOT/deps/BunnymodXT/build/libBunnymodXT.so"
exec "$G/lib/i386-linux-gnu/ld-linux.so.2" --library-path "$LIBS" --preload "$PRELOAD" \
	./hl_linux -steam -nobreakpad -nohdmodels "$@"
