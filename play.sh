#!/bin/bash
# Play a script headless and optionally log it with taslogger.
#   play.sh <script.hltas> <log.json|-> [map] [game] [engine args...]
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
S="$(realpath "$1")"; MAP="${3:-c1a0}"; GAME="${4:-valve}"
# Half-Life's command line splits on '-'.
case "$S" in *-*) echo "path contains '-': $S" >&2; exit 2;; esac
# Lets BXT apply the script's seed at the first map spawn instead of the clock.
export BXT_SCRIPT="$S"
LOG=()
if [ "$2" != "-" ]; then
	L="$(realpath -m "$2")"
	case "$L" in *-*) echo "path contains '-': $L" >&2; exit 2;; esac
	rm -f "$L"; LOG=(+bxt_taslog_filename "$L" +bxt_tas_write_log 1)
fi
"$ROOT/host/run.sh" -game "$GAME" "${LOG[@]}" +map "$MAP" +bxt_tas_loadscript "$S" "${@:5}"
