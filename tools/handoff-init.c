// SPDX-License-Identifier: GPL-2.0-only
/* Minimal PID 1 for a RAM-only handoff test. Writes a marker and reboots. */
#include <asm/unistd.h>

#ifndef TEST_MARKER
#define TEST_MARKER "KEXEC_HANDOFF_OK"
#endif

static long syscall6(long nr, long a, long b, long c, long d, long e, long f)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x4 __asm__("x4") = e;
    register long x5 __asm__("x5") = f;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x3),
                     "r"(x4), "r"(x5), "r"(x8) : "memory");
    return x0;
}

__attribute__((noreturn)) void _start(void)
{
    static const char message[] = "<6>" TEST_MARKER " pid=1\n";
    struct { long sec, nsec; } delay = {3, 0};
    long fd = syscall6(__NR_openat, -100, (long)"/dev/kmsg", 1, 0, 0, 0);
    if (fd >= 0) {
        syscall6(__NR_write, fd, (long)message, sizeof(message) - 1, 0, 0, 0);
        syscall6(__NR_close, fd, 0, 0, 0, 0, 0);
    }
    syscall6(__NR_nanosleep, (long)&delay, 0, 0, 0, 0, 0);
    syscall6(__NR_reboot, 0xfee1dead, 672274793, 0x01234567, 0, 0, 0);
    for (;;) syscall6(__NR_nanosleep, (long)&delay, 0, 0, 0, 0, 0);
}
