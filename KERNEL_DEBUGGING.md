# Capturing the kernel crash in QEMU

This starts from a stopped VM. The QEMU window will show Alpine, and the host
will save the guest's serial console to `serial.log`. The kernel command line
must send output to both the graphical console (`tty0`) and serial port
(`ttyS0`); otherwise the crash trace will appear in the window but not the log.

## 1. Start the VM from the host

Run this on the host. It opens the QEMU GUI and writes serial output to the log:

```sh
qemu-system-x86_64 \
  -enable-kvm \
  -m 2048 \
  -smp 2 \
  -drive file="$HOME/Vms/lager/alpine.qcow2",format=qcow2 \
  -boot order=c \
  -nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22 \
  -chardev file,id=serial0,path="$HOME/Vms/lager/serial.log" \
  -serial chardev:serial0
```

Do not start a second QEMU process with the same disk image.

## 2. Configure Alpine inside the VM

Log in at the Alpine prompt in the QEMU window. Check the boot files:

```sh
ls /boot
```

If `/boot/extlinux.conf` exists, edit it as root:

```sh
vi /boot/extlinux.conf
```

On the `APPEND` line for the boot entry you use, preserve the existing options
and append:

```text
console=ttyS0,115200n8 console=tty0
```

Save and reboot Alpine. After logging in again, verify the active kernel
command line:

```sh
cat /proc/cmdline
```

It should include both `console=ttyS0,115200n8` and `console=tty0`. If
`/boot/extlinux.conf` does not exist, inspect `ls /boot` and identify the
bootloader before editing a config file.

Important: the serial option must say `console=ttyS0,115200n8`, not
`console=tty0,115200n8`. If `/proc/cmdline` shows the wrong value, edit the
`APPEND` line in `/boot/extlinux.conf` again so its console options are exactly
`console=ttyS0,115200n8 console=tty0`, then reboot.

## 3. Verify the serial capture before crashing

In the guest, check that Linux registered both consoles:

```sh
cat /proc/consoles
```

The output should list both `ttyS0` and `tty0`. If it lists only `tty0`, the
serial console is not enabled yet. Then, in a host terminal, watch the serial
log:

```sh
tail -f "$HOME/Vms/lager/serial.log"
```

From the guest, emit a test kernel message (the guest prompt is root in this
VM):

```sh
echo '<0>SERIAL-CAPTURE-TEST' > /dev/kmsg
```

Confirm `SERIAL-CAPTURE-TEST` appears in the host's `serial.log`. If it does
not, do not reproduce the crash yet: check that `/proc/cmdline` contains both
console options and `/proc/consoles` lists `ttyS0`.

## 4. Reproduce and collect the crash

With the same QEMU process still open, reproduce the crash in Alpine. Then,
from a host terminal, read the captured log:

```sh
cat "$HOME/Vms/lager/serial.log"
```

The crash trace should now appear in the serial log as well as the QEMU window.

## Notes

- The graphical QEMU window shows the guest's VGA console. `serial.log` records
  only the emulated serial port. The two `console=` options route kernel output
  to both.
- `console=ttyS0,115200n8` is a Linux kernel boot parameter. It belongs in the
  guest bootloader's kernel options, not as a standalone QEMU shell argument.
- KGDB remains unavailable in this guest kernel because its config says
  `# CONFIG_KGDB is not set`. KGDB requires a kernel built with KGDB support;
  serial crash logging does not.
