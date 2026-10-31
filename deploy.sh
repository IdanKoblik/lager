#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
TARGET="${1:-${SSH_TARGET:-}}"
PORT="${SSH_PORT:-2222}"
SSH_CONFIG_ARGS=()
if [ -n "${SSH_CONFIG:-}" ]; then
    SSH_CONFIG_ARGS=(-F "$SSH_CONFIG")
fi
STAGE="${REMOTE_STAGE:-/tmp/lager-deploy}"
TMP="$(mktemp -d)"
ARCHIVE="$TMP/lager-source.tar.gz"

fail() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

trap 'rm -rf "$TMP"' EXIT

[ -n "$TARGET" ] || fail "usage: $0 <ssh-target>"
[ -f "$ROOT/notch/Cargo.toml" ] || fail "notch submodule missing; run git submodule update --init --recursive"

tar -C "$ROOT" --exclude='./.git' --exclude='./notch/.git' --exclude='./notch/target' --exclude='./build' -czf "$ARCHIVE" Makefile kernel notch
ssh "${SSH_CONFIG_ARGS[@]}" -p "$PORT" "$TARGET" "mkdir -p '$STAGE/src'"
scp "${SSH_CONFIG_ARGS[@]}" -P "$PORT" "$ARCHIVE" "$TARGET:$STAGE/"

ssh "${SSH_CONFIG_ARGS[@]}" -p "$PORT" "$TARGET" "sh -s '$STAGE'" <<'REMOTE'
set -eu
STAGE="$1"
KERNEL="$(uname -r)"
KDIR="/lib/modules/$KERNEL/build"

apk add --no-cache build-base cargo kmod libpcap-dev linux-virt-dev pkgconf rust
tar -xzf "$STAGE/lager-source.tar.gz" -C "$STAGE/src"
make -C "$STAGE/src" KDIR="$KDIR" module
[ "$(make -s -C "$KDIR" kernelrelease)" = "$KERNEL" ]
(cd "$STAGE/src/notch" && cargo build --release --bin notch)

install -D -m 0644 "$STAGE/src/build/lager.ko" "/lib/modules/$KERNEL/extra/lager.ko"
depmod -a "$KERNEL"
install -D -m 0755 "$STAGE/src/notch/target/release/notch" /usr/local/bin/notch
modprobe -r lager
modprobe lager
printf 'deployed lager and notch to %s\n' "$(hostname)"
REMOTE
