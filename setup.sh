#!/bin/bash
# Fetch and build everything goldfork needs into deps/ and host/build/.
# Safe to rerun: finished steps are skipped.
set -euo pipefail
cd "$(dirname "$0")"
for c in bwrap curl git sha256sum; do
	command -v $c >/dev/null || { echo "setup.sh needs $c" >&2; exit 1; }
done
mkdir -p deps
ubuntu() { host/ubuntu.sh "$@"; }

fetch() {  # <url> <sha256> <file>
	[ -f "$3" ] || { curl -fsSL -o "$3.part" "$1" && mv "$3.part" "$3"; }
	echo "$2  $3" | sha256sum -c --quiet
}

# Ubuntu 22.04 with GCC 11.4, the compiler of BXT's Linux releases.
if [ ! -f deps/ubuntu/.ready ]; then
	echo "== Ubuntu 22.04 environment"
	fetch https://cdimage.ubuntu.com/ubuntu-base/releases/22.04/release/ubuntu-base-22.04.5-base-amd64.tar.gz \
		242cd8898b33ea806ef5f13b1076ed7c76f9f989d18384452f7166692438ff1a deps/ubuntu-base.tar.gz
	mkdir -p deps/ubuntu && tar -xzf deps/ubuntu-base.tar.gz -C deps/ubuntu
	ubuntu sh -c 'dpkg --add-architecture i386 &&
		apt-get -qq -o APT::Sandbox::User=root update &&
		apt-get -qq -o APT::Sandbox::User=root install -y --no-install-recommends \
			g++-multilib cmake ninja-build git curl ca-certificates libboost-dev libgl-dev:i386 \
			mesa-common-dev xvfb locales >/dev/null'
	touch deps/ubuntu/.ready
fi

# Rust, for BXT's script parser (hltas).
if [ ! -x deps/ubuntu/root/.cargo/bin/cargo ]; then
	echo "== Rust"
	ubuntu sh -c 'curl -fsSL https://sh.rustup.rs | sh -s -- -q -y --profile minimal \
		--default-toolchain 1.100.0 --target i686-unknown-linux-gnu >/dev/null'
fi

# glibc 2.35 (Ubuntu 22.04's release build): the game runs on it, for libm
# results identical to BXT's own test recordings.
if [ ! -x deps/glibc/lib/i386-linux-gnu/ld-linux.so.2 ]; then
	echo "== glibc 2.35"
	P=http://archive.ubuntu.com/ubuntu/pool/main/g
	fetch $P/glibc/libc6_2.35-0ubuntu3_i386.deb \
		1f2b0f8c373e0bc64f86436bfe2f9b575a58e51f629a918202ac6d27e45ddbf9 deps/libc6.deb
	fetch $P/gcc-12/libgcc-s1_12-20220319-1ubuntu1_i386.deb \
		8652c6225760de73dda330871f00bd549c64d5e89688d35a699ed84101e7d08c deps/libgcc-s1.deb
	fetch $P/gcc-12/libstdc++6_12-20220319-1ubuntu1_i386.deb \
		1b7351a648626b624927ff2089c14dd933ef19cf087aa5806d8612b3639311c1 deps/libstdc++6.deb
	for d in libc6 libgcc-s1 libstdc++6; do ubuntu dpkg-deb -x /repo/deps/$d.deb /repo/deps/glibc; done
fi

# A UTF-8 locale, so the game shows no warning dialog.
if [ ! -d deps/locale/en_US.UTF-8 ]; then
	echo "== locale"
	mkdir -p deps/locale
	ubuntu localedef -i en_US -f UTF-8 /repo/deps/locale/en_US.UTF-8
fi

# Steam's scout runtime: libraries for the game's browser component.
if [ ! -x deps/steam-runtime/run.sh ]; then
	echo "== Steam runtime"
	fetch https://repo.steampowered.com/steamrt-images-scout/snapshots/1.0.20261021.266054/steam-runtime.tar.xz \
		c7b47b3bd84281c22f93ec865ca263c1a1bcb2eb74efafe6d2e722c800985904 deps/steam-runtime.tar.xz
	tar -xJf deps/steam-runtime.tar.xz -C deps
fi

# Bunnymod XT with goldfork's patch.
if [ ! -f deps/BunnymodXT/build/libBunnymodXT.so ]; then
	echo "== Bunnymod XT"
	[ -d deps/BunnymodXT ] || git clone -q https://github.com/YaLTeR/BunnymodXT.git deps/BunnymodXT
	git -C deps/BunnymodXT checkout -q 9ec20d9534f7c1347a362bc85efcb6f3115994f6
	git -C deps/BunnymodXT submodule update -q --init --recursive
	git -C deps/BunnymodXT apply --reverse --check "$PWD/patches/bunnymodxt.patch" 2>/dev/null ||
		git -C deps/BunnymodXT apply "$PWD/patches/bunnymodxt.patch"
	ubuntu sh -c 'cmake -S /repo/deps/BunnymodXT -B /repo/deps/BunnymodXT/build -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null &&
		ninja -C /repo/deps/BunnymodXT/build >/dev/null'
fi

echo "== host libraries"
host/build.sh
echo "Done. Next: python3 tests/bitexact.py"
