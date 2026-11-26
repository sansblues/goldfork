#!/bin/bash
# Build the host libraries in the Ubuntu 22.04 environment, so they need
# nothing newer than the pinned glibc 2.35.
set -e
exec "$(dirname "$0")/ubuntu.sh" bash -c '
set -e; cd /repo/host; mkdir -p build/gl build/steam build/sdl
CF="-m32 -O2 -fPIC -shared -fvisibility=hidden -Wall"
gcc $CF -DNULLGL_LIBGL -Wl,-soname,libGL.so.1 -o build/gl/libGL.so.1 nullgl.c -ldl
gcc $CF -o build/libgoldfork.so nullgl.c hooks.c -ldl -lpthread
gcc $CF -Wl,-soname,libsteam_api.so -o build/steam/libsteam_api.so steamstub.c
# The game bundles a 2013 SDL that crashes under the dummy video driver.
ln -sf ../../../deps/steam-runtime/usr/lib/i386-linux-gnu/libSDL2-2.0.so.0 build/sdl/'
