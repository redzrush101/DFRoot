# kexec for Android 12 / Linux 5.10

Port of [sarabpal-dev/kexec_mod](https://github.com/sarabpal-dev/kexec_mod), with the stock kernel shutdown path, segment validation, and a libfdt-based loader.

Checked on a DOOGEE T30 Pro running `5.10.198-android12-9`: module insertion/removal, root-only status ioctls, stock Image staging/unloading, and filesystem freeze/thaw after a refused execution. The tablet boots at EL1 and protected KVM is inactive. The first handoff attempt stopped after CPU shutdown without a second-kernel marker. The relocation stub now preserves the live kernel MAIR entries; a second test is pending.

Build the module with the matching Android 12 5.10 DDK and Clang 12:

```sh
make -C "$KDIR" M="$PWD" ARCH=arm64 LLVM=1 LLVM_IAS=1 CONFIG_KEXEC_MOD=m modules
```

Build the Android userspace tools with a native Termux compiler or NDK compiler:

```sh
git submodule update --init --recursive
make -C tools CC=clang
```

Run as root. Use an uncompressed ARM64 Image and a load address that matches its header and the device memory map:

```sh
insmod kexec_mod.ko enable_exec=1
./kexec-load Image --load ADDRESS --dtb board.dtb --initrd initramfs.cpio.gz --cmdline '...'
./kexec-load --status
./kexec-load --exec
```

`--exec` syncs and freezes writable ext4/f2fs filesystems before the handoff. A refused ioctl thaws them again. A successful jump replaces Android. The module refuses execution on an EL2 boot or with active protected KVM. Module insertion alone never starts a kernel.

To discard a staged image:

```sh
./kexec-load --unload
rmmod kexec_mod
```

The test in `tools/kexec-selftest.c` exercises LOAD/UNLOAD and invalid inputs without issuing EXEC. `tools/handoff-init.c` and `tools/make-test-initramfs.py` create a minimal RAM-only test rootfs: PID 1 writes a marker to `/dev/kmsg`, waits three seconds, and requests a normal reboot. They do not mount storage.
