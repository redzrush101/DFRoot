# Device test

DOOGEE T30 Pro, Android 14, stock kernel `5.10.198-android12-9-00089-g347e22ab61ee-ab11279209`.

Tested module source: `bce8453d37737ec33e6cb55a93e3d1fbe2065ebf`.
Build: https://github.com/redzrush101/DFRoot/actions/runs/42029985510

All 13 LOAD/UNLOAD checks passed: invalid inputs, a valid staging operation, preservation of the previous image after a failed replacement, and unload/status checks. A refused execution also thawed the persistent filesystems and returned to Android.

The second handoff booted the unmodified stock Image from RAM with a small test initramfs. PID 1 wrote the marker below, then requested a normal reboot after three seconds. The kernel log was recovered from pstore after Android restarted. No partitions were flashed or resized.

```text
[    0.000000][    T0] Linux version 5.10.198-android12-9-00089-g347e22ab61ee-ab11279209 (build-user@build-host) (Android (7284624, based on r416183b) clang version 12.0.5 (https://android.googlesource.com/toolchain/llvm-project c935d99d7cf2016289302412d708641d52d2f7ee), LLD 12.0.5 (/buildbot/src/android/llvm-toolchain/out/llvm-project/lld c935d99d7cf2016289302412d708641d52d2f7ee)) #1 SMP PREEMPT Fri Jan 5 10:02:31 UTC 2024
[    0.503120][    T1] Run /init as init process
[    0.503719][    T1] KEXEC_HANDOFF_OK_T30_20261105_A2 pid=1
[    3.507210][    T1] reboot: Restarting system
```
