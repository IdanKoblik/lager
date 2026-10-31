# Lager

Experimental Kernel-level Network Steganalysis and Prevention.
The project is under active development.

## Requirements

On the host, install:

- QEMU with x86 system emulation and `qemu-img`
- OpenSSH client (`ssh` and `scp`)
- Git

For example, on Arch Linux:

```sh
sudo pacman -S qemu-full openssh git
```

On Debian or Ubuntu:

```sh
sudo apt install qemu-system-x86 qemu-utils openssh-client git
```

The script uses an Alpine disk image at `~/Vms/lager/alpine.qcow2` by default.
The image needs root SSH access using a key available to the host, an SSH
server, and an Alpine boot partition with `/boot/extlinux.conf`. You can choose
another image with `BASE_IMAGE=/path/to/alpine.qcow2`.

Initialize the Notch submodule:

```sh
git submodule update --init --recursive
```

## Run the VM lab

Start the lab from the repository root:

```sh
./deploy-lab.sh
```

The script creates copy-on-write disk overlays, so it leaves the base image
unchanged. It opens two QEMU windows. Each VM has a user-network interface for
SSH management and a second interface connected to the other VM through a
QEMU socket link. That link is isolated from the host network. The guests are
named `sender` and `receiver`, with addresses `192.168.77.1` and
`192.168.77.2`. The script pings the receiver from the sender before injecting
the payload and stops if the link is unreachable.

The script enables serial kernel logging, including debug-level `printk`
messages, in each Alpine overlay. It deploys the
Lager module to the receiver, and puts the Notch executable on the sender only.
It then automatically sends a 120-byte printable ICMP payload. The receiver's
serial log path is printed by the script; follow it from another host terminal
with the printed `tail -f` command. QEMU process logs and VM overlays are saved
in the same run directory, under `~/Vms/lager/`.

Press Ctrl+C in the terminal running the script to stop both VMs. The run
directory is kept for inspecting logs and overlays.

## Manual module build

To build Lager against the currently running kernel, install that kernel's
development headers and run:

```sh
make
```

The module is written to `build/lager.ko`. The VM deployment script builds it
against the receiver's own kernel headers instead of the host's. Builds use
the available processor count by default; override it with `make JOBS=2`.
