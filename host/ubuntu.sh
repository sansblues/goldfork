#!/bin/bash
# Run a command in the Ubuntu 22.04 environment (deps/ubuntu) via bwrap,
# unprivileged. The repository is mounted at /repo.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
exec bwrap --unshare-user --uid 0 --gid 0 --bind "$ROOT/deps/ubuntu" / --proc /proc --dev /dev \
	--tmpfs /tmp --ro-bind /etc/resolv.conf /etc/resolv.conf --bind "$ROOT" /repo \
	--setenv HOME /root --setenv PATH /root/.cargo/bin:/usr/sbin:/usr/bin:/sbin:/bin \
	--setenv DEBIAN_FRONTEND noninteractive "$@"
