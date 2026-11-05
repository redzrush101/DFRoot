// SPDX-License-Identifier: GPL-2.0-only
/* LOAD/UNLOAD regression checks. Never issues EXEC. Run as root. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "../kexec_mod_user.h"

static int fd;

static void expect(const char *name, struct kexec_mod_load_args *args, int error)
{
    errno = 0;
    int ret = ioctl(fd, KEXEC_MOD_IOCTL_LOAD, args);
    if ((error && (ret != -1 || errno != error)) || (!error && ret)) {
        fprintf(stderr, "FAIL %s: return=%d errno=%d, expected=%d\n", name, ret, errno, error);
        exit(1);
    }
    printf("PASS %s\n", name);
}

static void status(int expected)
{
    int actual = -1;
    if (ioctl(fd, KEXEC_MOD_IOCTL_STATUS, &actual) || actual != expected) {
        fprintf(stderr, "FAIL image status: %d, expected %d\n", actual, expected);
        exit(1);
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s RAM_ADDRESS\n", argv[0]);
        return 1;
    }
    char *end;
    errno = 0;
    unsigned long mem = strtoul(argv[1], &end, 0);
    if (errno || *end || !mem || (mem & 4095) || mem > UINT64_MAX - 8192)
        return 1;
    unsigned char bytes[4096] = {0};
    struct kexec_mod_segment segs[2] = {{bytes, sizeof(bytes), mem, 4096}};
    struct kexec_mod_load_args args = {.entry = mem, .nr_segments = 1, .segments = segs};
    fd = open(KEXEC_MOD_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror(KEXEC_MOD_DEVICE); return 1; }
    status(0);
    expect("bad argument pointer", NULL, EFAULT);
    args.flags = 1;
    expect("unsupported flags", &args, EINVAL);
    args.flags = 0; args.nr_segments = 17;
    expect("too many segments", &args, EINVAL);
    args.nr_segments = 1; segs[0].memsz = 0;
    expect("zero memory size", &args, EADDRNOTAVAIL);
    segs[0].memsz = 4096; segs[0].mem++;
    expect("unaligned destination", &args, EADDRNOTAVAIL);
    segs[0].mem = 0; args.entry = 0;
    expect("non-RAM destination", &args, EADDRNOTAVAIL);
    segs[0].mem = mem; args.entry = mem + 4096;
    expect("entry outside image", &args, EINVAL);
    args.entry = mem; segs[1] = segs[0]; args.nr_segments = 2;
    expect("overlapping segments", &args, EINVAL);
    args.nr_segments = 1; segs[0].bufsz = 4097;
    expect("buffer larger than destination", &args, EINVAL);
    segs[0].bufsz = 4096; segs[0].buf = NULL;
    expect("bad buffer pointer", &args, EFAULT);
    status(0);
    segs[0].buf = bytes;
    expect("stage image", &args, 0);
    status(1);
    segs[0].buf = NULL;
    expect("failed replacement preserves old image", &args, EFAULT);
    status(1);
    if (ioctl(fd, KEXEC_MOD_IOCTL_UNLOAD)) { perror("unload"); return 1; }
    status(0);
    close(fd);
    puts("PASS unload and status");
    return 0;
}
