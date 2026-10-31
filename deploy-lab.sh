#!/usr/bin/env bash
set -euo pipefail

# Boot two disposable Alpine guests on an isolated QEMU socket link, deploy
# lager only to the receiver, and send a printable ICMP payload from the sender.

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BASE_IMAGE="${BASE_IMAGE:-$HOME/Vms/lager/alpine.qcow2}"
QEMU="${QEMU:-qemu-system-x86_64}"
QEMU_IMG="${QEMU_IMG:-qemu-img}"
SENDER_SSH_PORT="${SENDER_SSH_PORT:-2223}"
RECEIVER_SSH_PORT="${RECEIVER_SSH_PORT:-2224}"
LAB_SOCKET_PORT="${LAB_SOCKET_PORT:-45678}"
SENDER_IP="192.168.77.1"
RECEIVER_IP="192.168.77.2"
SENDER_MAC="52:54:00:77:00:01"
RECEIVER_MAC="52:54:00:77:00:02"
SSH_OPTS=(-F /dev/null -o BatchMode=yes -o ConnectTimeout=3 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null)

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

for cmd in "$QEMU" "$QEMU_IMG" ssh scp; do
    command -v "$cmd" >/dev/null 2>&1 || die "required command not found: $cmd"
done
[ -f "$BASE_IMAGE" ] || die "Alpine disk image not found: $BASE_IMAGE (set BASE_IMAGE=/path/to/alpine.qcow2)"
[ -f "$ROOT/deploy.sh" ] || die "deploy.sh not found beside this script"
[ -f "$ROOT/notch/Cargo.toml" ] || die "notch submodule is missing; run: git submodule update --init --recursive"

RUN_DIR="$(mktemp -d "$HOME/Vms/lager/lab-run.XXXXXX")"
SENDER_PID=""
RECEIVER_PID=""
SSH_CONFIG_FILE="$RUN_DIR/ssh_config"
cat > "$SSH_CONFIG_FILE" <<'SSHCONF'
Host *
    BatchMode yes
    StrictHostKeyChecking no
    UserKnownHostsFile /dev/null
SSHCONF

cleanup() {
    trap - EXIT INT TERM
    [ -z "$SENDER_PID" ] || kill "$SENDER_PID" 2>/dev/null || true
    [ -z "$RECEIVER_PID" ] || kill "$RECEIVER_PID" 2>/dev/null || true
    [ -z "$SENDER_PID" ] || wait "$SENDER_PID" 2>/dev/null || true
    [ -z "$RECEIVER_PID" ] || wait "$RECEIVER_PID" 2>/dev/null || true
    printf '\nQEMU guests stopped. Disk overlays and logs are in %s\n' "$RUN_DIR"
}
trap cleanup EXIT INT TERM

ssh_root() {
    local port="$1"
    shift
    ssh "${SSH_OPTS[@]}" -p "$port" "root@127.0.0.1" "$@"
}

wait_for_ssh() {
    local port="$1"
    local role="$2"
    local attempts=0
    until ssh_root "$port" true >/dev/null 2>&1; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 60 ]; then
            die "$role SSH did not become ready on 127.0.0.1:$port; see $RUN_DIR/$role-serial.log"
        fi
        sleep 2
    done
}

enable_serial_console() {
    local port="$1"
    local role="$2"
    ssh_root "$port" sh -s <<'REMOTE'
set -eu
conf=/boot/extlinux.conf
[ -f "$conf" ] || { echo "$conf not found; configure console=ttyS0,115200n8 console=tty0 in the guest bootloader" >&2; exit 1; }
sed -i -E '/^[[:space:]]*APPEND([[:space:]]|$)/ {
    s/[[:space:]]console=[^[:space:]]+//g
    s/[[:space:]]ignore_loglevel//g
    s/[[:space:]]*$//
    s/$/ console=ttyS0,115200n8 console=tty0 ignore_loglevel/
}' "$conf"
REMOTE
    printf 'Enabling serial kernel logging on %s and rebooting it...\n' "$role"
    ssh_root "$port" reboot >/dev/null 2>&1 || true
    sleep 2
    wait_for_ssh "$port" "$role"
}

configure_lab_nic() {
    local port="$1"
    local mac="$2"
    local address="$3"
    ssh_root "$port" sh -s -- "$mac" "$address" <<'REMOTE'
set -eu
mac="$1"
address="$2"
iface=""
for path in /sys/class/net/*; do
    if [ "$(cat "$path/address")" = "$mac" ]; then
        iface="${path##*/}"
        break
    fi
done
[ -n "$iface" ] || { echo "lab NIC with MAC $mac not found" >&2; exit 1; }
ip link set "$iface" up
ip addr flush dev "$iface"
ip addr add "$address/24" dev "$iface"
printf '%s\n' "$iface"
REMOTE
}

set_guest_hostname() {
    local port="$1"
    local name="$2"
    ssh_root "$port" sh -s -- "$name" <<'REMOTE'
set -eu
name="$1"
printf '%s\n' "$name" > /etc/hostname
hostname "$name"
REMOTE
}

"$QEMU_IMG" create -q -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$RUN_DIR/sender.qcow2"
"$QEMU_IMG" create -q -f qcow2 -F qcow2 -b "$BASE_IMAGE" "$RUN_DIR/receiver.qcow2"

printf 'Starting receiver QEMU window...\n'
"$QEMU" \
    -enable-kvm -m 2048 -smp 2 \
    -drive "file=$RUN_DIR/receiver.qcow2,format=qcow2" \
    -boot order=c \
    -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:$RECEIVER_SSH_PORT-:22" \
    -netdev "socket,id=lab,listen=127.0.0.1:$LAB_SOCKET_PORT" \
    -device "virtio-net-pci,netdev=lab,mac=$RECEIVER_MAC" \
    -chardev "file,id=serial0,path=$RUN_DIR/receiver-serial.log" \
    -serial chardev:serial0 \
    >"$RUN_DIR/receiver-qemu.log" 2>&1 &
RECEIVER_PID=$!

sleep 2
printf 'Starting sender QEMU window...\n'
"$QEMU" \
    -enable-kvm -m 2048 -smp 2 \
    -drive "file=$RUN_DIR/sender.qcow2,format=qcow2" \
    -boot order=c \
    -nic "user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:$SENDER_SSH_PORT-:22" \
    -netdev "socket,id=lab,connect=127.0.0.1:$LAB_SOCKET_PORT" \
    -device "virtio-net-pci,netdev=lab,mac=$SENDER_MAC" \
    -chardev "file,id=serial0,path=$RUN_DIR/sender-serial.log" \
    -serial chardev:serial0 \
    >"$RUN_DIR/sender-qemu.log" 2>&1 &
SENDER_PID=$!

printf 'Waiting for Alpine SSH...\n'
wait_for_ssh "$RECEIVER_SSH_PORT" receiver
wait_for_ssh "$SENDER_SSH_PORT" sender
enable_serial_console "$RECEIVER_SSH_PORT" receiver
enable_serial_console "$SENDER_SSH_PORT" sender
set_guest_hostname "$RECEIVER_SSH_PORT" receiver
set_guest_hostname "$SENDER_SSH_PORT" sender

printf 'Assigning isolated lab addresses...\n'
RECEIVER_IFACE="$(configure_lab_nic "$RECEIVER_SSH_PORT" "$RECEIVER_MAC" "$RECEIVER_IP")"
SENDER_IFACE="$(configure_lab_nic "$SENDER_SSH_PORT" "$SENDER_MAC" "$SENDER_IP")"

printf 'Deploying lager to receiver only...\n'
SSH_CONFIG="$SSH_CONFIG_FILE" SSH_PORT="$RECEIVER_SSH_PORT" "$ROOT/deploy.sh" root@127.0.0.1
scp -F /dev/null -P "$RECEIVER_SSH_PORT" -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    "root@127.0.0.1:/usr/local/bin/notch" "$RUN_DIR/notch"
ssh_root "$RECEIVER_SSH_PORT" rm -f /usr/local/bin/notch
ssh_root "$RECEIVER_SSH_PORT" sh -c 'lsmod | grep -q "^lager "' || die "lager did not load on receiver"

printf 'Installing Notch on sender only...\n'
scp -F /dev/null -P "$SENDER_SSH_PORT" -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    "$RUN_DIR/notch" "root@127.0.0.1:/tmp/notch-lab"
ssh_root "$SENDER_SSH_PORT" sh -s <<'REMOTE'
set -eu
install -m 0755 /tmp/notch-lab /usr/local/bin/notch
rm -f /tmp/notch-lab
modprobe -r lager 2>/dev/null || true
rm -f "/lib/modules/$(uname -r)/extra/lager.ko"
depmod -a
REMOTE

head -c 120 /dev/zero | tr '\000' 'A' > "$RUN_DIR/payload.bin"
scp -F /dev/null -P "$SENDER_SSH_PORT" -o BatchMode=yes -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
    "$RUN_DIR/payload.bin" "root@127.0.0.1:/tmp/lager-payload.bin"

printf 'Receiver: %s (lab NIC %s), lager loaded\n' "$RECEIVER_IP" "$RECEIVER_IFACE"
printf 'Sender:   %s (lab NIC %s)\n' "$SENDER_IP" "$SENDER_IFACE"
printf 'Checking sender-to-receiver connectivity...\n'
ssh_root "$SENDER_SSH_PORT" ping -I "$SENDER_IFACE" -c 1 -W 2 "$RECEIVER_IP"
printf 'Sending a 120-byte printable payload; receiver log: %s/receiver-serial.log\n' "$RUN_DIR"
ssh_root "$SENDER_SSH_PORT" /usr/local/bin/notch inject \
    --channel icmp --no-auth --dest "$RECEIVER_IP" --interface "$SENDER_IFACE" \
    --payload /tmp/lager-payload.bin --rate 10 --confirm

printf '\nLab is running. Follow the receiver log with:\n  tail -f %q\n' "$RUN_DIR/receiver-serial.log"
printf 'Press Ctrl-C here to stop both guests.\n'
wait -n "$SENDER_PID" "$RECEIVER_PID"
