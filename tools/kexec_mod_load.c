// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/fs.h>
#include <mntent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libfdt.h>
#include "../kexec_mod_user.h"

#define PAGE_SIZE 4096UL
#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((a) - 1))

static void die(const char *what)
{
    perror(what);
    exit(EXIT_FAILURE);
}

static void fdt_check(int ret)
{
    if (ret < 0) {
        fprintf(stderr, "DTB: %s\n", fdt_strerror(ret));
        exit(EXIT_FAILURE);
    }
}

static void *read_file(const char *path, size_t *size)
{
    struct stat st;
    FILE *f = fopen(path, "rb");
    if (!f || fstat(fileno(f), &st)) die(path);
    if (st.st_size <= 0 || (uint64_t)st.st_size > SIZE_MAX - 8192) {
        fprintf(stderr, "%s: expected a nonempty regular file\n", path);
        exit(EXIT_FAILURE);
    }
    *size = st.st_size;
    void *buf = calloc(1, *size + 8192);
    if (!buf) die("calloc");
    if (fread(buf, 1, *size, f) != *size) {
        fprintf(stderr, "%s: short read\n", path);
        exit(EXIT_FAILURE);
    }
    fclose(f);
    return buf;
}

static uint64_t le64(const unsigned char *p)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* Keep persistent filesystems consistent while replacing the running kernel.
 * Open everything first; once /data is frozen, no file writes are possible.
 * An ioctl failure thaws every filesystem before returning to Android. */
static int execute(int fd)
{
    struct { int fd; dev_t dev; } mounts[64];
    size_t count = 0, frozen = 0;
    FILE *table = setmntent("/proc/mounts", "r");
    if (!table) return -1;
    struct mntent *m;
    while ((m = getmntent(table))) {
        if ((strcmp(m->mnt_type, "ext4") && strcmp(m->mnt_type, "f2fs")) ||
            (strncmp(m->mnt_opts, "rw,", 3) && strcmp(m->mnt_opts, "rw"))) continue;
        int f = open(m->mnt_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        struct stat st;
        if (f < 0 || fstat(f, &st)) {
            if (f >= 0) close(f);
            endmntent(table);
            for (size_t i = 0; i < count; i++) close(mounts[i].fd);
            return -1;
        }
        size_t i;
        for (i = 0; i < count; i++) if (mounts[i].dev == st.st_dev) break;
        if (i < count) { close(f); continue; }
        if (count == 64) {
            close(f); endmntent(table);
            for (i = 0; i < count; i++) close(mounts[i].fd);
            errno = E2BIG;
            return -1;
        }
        mounts[count].fd = f; mounts[count++].dev = st.st_dev;
    }
    endmntent(table);
    sync();
    int ret = -1;
    while (frozen < count) {
        if (ioctl(mounts[frozen].fd, FIFREEZE, 0)) goto thaw;
        frozen++;
    }
    ret = ioctl(fd, KEXEC_MOD_IOCTL_EXEC);
thaw:;
    int saved = errno;
    while (frozen) {
        if (ioctl(mounts[--frozen].fd, FITHAW, 0))
            perror("thaw filesystem");
    }
    for (size_t i = 0; i < count; i++) close(mounts[i].fd);
    errno = saved;
    return ret;
}

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s Image --load ADDRESS --dtb FILE [--initrd FILE] [--cmdline TEXT]\n"
                    "       %s --status | --unload | --exec\n", prog, prog);
}

int main(int argc, char **argv)
{
    static const struct option opts[] = {
        {"load", required_argument, NULL, 'l'}, {"dtb", required_argument, NULL, 'd'},
        {"initrd", required_argument, NULL, 'i'}, {"cmdline", required_argument, NULL, 'c'},
        {"status", no_argument, NULL, 's'}, {"unload", no_argument, NULL, 'u'},
        {"exec", no_argument, NULL, 'e'}, {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    const char *dtb_path = NULL, *initrd_path = NULL, *cmdline = NULL;
    unsigned long load = 0;
    int operation = 0, ch;
    while ((ch = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (ch) {
        case 'l': {
            char *end;
            errno = 0;
            load = strtoul(optarg, &end, 0);
            if (errno || *end || !load) { usage(argv[0]); return EXIT_FAILURE; }
            break;
        }
        case 'd': dtb_path = optarg; break;
        case 'i': initrd_path = optarg; break;
        case 'c': cmdline = optarg; break;
        case 's': case 'u': case 'e':
            if (operation) { usage(argv[0]); return EXIT_FAILURE; }
            operation = ch;
            break;
        case 'h': usage(argv[0]); return EXIT_SUCCESS;
        default: usage(argv[0]); return EXIT_FAILURE;
        }
    }
    if ((operation && optind != argc) ||
        (!operation && (optind + 1 != argc || !load || !dtb_path))) {
        usage(argv[0]); return EXIT_FAILURE;
    }
    int fd = open(KEXEC_MOD_DEVICE, O_RDWR | O_CLOEXEC);
    if (fd < 0) die(KEXEC_MOD_DEVICE);
    if (operation) {
        int status = 0;
        unsigned long request = operation == 's' ? KEXEC_MOD_IOCTL_STATUS :
                                operation == 'u' ? KEXEC_MOD_IOCTL_UNLOAD : KEXEC_MOD_IOCTL_EXEC;
        if (operation == 'e') {
            if (execute(fd)) die("kexec execute");
        } else if (ioctl(fd, request, &status)) die("kexec ioctl");
        if (operation == 's') puts(status ? "Image loaded" : "No image loaded");
        close(fd);
        return EXIT_SUCCESS;
    }

    size_t kernel_size, dtb_size, initrd_size = 0;
    unsigned char *kernel = read_file(argv[optind], &kernel_size);
    if (kernel_size < 64 || memcmp(kernel + 56, "ARM\x64", 4)) {
        fprintf(stderr, "Expected an uncompressed ARM64 Image\n"); return EXIT_FAILURE;
    }
    uint64_t text_offset = le64(kernel + 8), image_size = le64(kernel + 16);
    if (!image_size || text_offset >= 2 * 1024 * 1024 || load < text_offset ||
        (load - text_offset) % (2 * 1024 * 1024)) {
        fprintf(stderr, "Load address must follow text_offset and 2 MiB alignment\n");
        return EXIT_FAILURE;
    }
    size_t footprint = image_size > kernel_size ? image_size : kernel_size;
    if (load > ULONG_MAX - PAGE_SIZE || footprint > ULONG_MAX - load - PAGE_SIZE) {
        fprintf(stderr, "Image address overflow\n"); return EXIT_FAILURE;
    }
    footprint = ALIGN_UP(footprint, PAGE_SIZE);
    void *dtb = read_file(dtb_path, &dtb_size);
    fdt_check(fdt_check_full(dtb, dtb_size));
    if (dtb_size > INT_MAX - 8192) {
        fprintf(stderr, "DTB too large\n"); return EXIT_FAILURE;
    }
    fdt_check(fdt_open_into(dtb, dtb, dtb_size + 8192));
    unsigned long dtb_mem = load + footprint;
    unsigned long initrd_mem = dtb_mem + ALIGN_UP(dtb_size + 8192, PAGE_SIZE);
    void *initrd = initrd_path ? read_file(initrd_path, &initrd_size) : NULL;
    if (initrd_mem < dtb_mem || initrd_mem > ULONG_MAX - PAGE_SIZE ||
        initrd_size > ULONG_MAX - initrd_mem - PAGE_SIZE) {
        fprintf(stderr, "Initrd address overflow\n"); return EXIT_FAILURE;
    }
    int chosen = fdt_path_offset(dtb, "/chosen");
    if (chosen == -FDT_ERR_NOTFOUND) chosen = fdt_add_subnode(dtb, 0, "chosen");
    fdt_check(chosen);
    if (cmdline) fdt_check(fdt_setprop_string(dtb, chosen, "bootargs", cmdline));
    if (initrd) {
        fdt_check(fdt_setprop_u64(dtb, chosen, "linux,initrd-start", initrd_mem));
        fdt_check(fdt_setprop_u64(dtb, chosen, "linux,initrd-end", initrd_mem + initrd_size));
    } else {
        int ret = fdt_delprop(dtb, chosen, "linux,initrd-start");
        if (ret != -FDT_ERR_NOTFOUND) fdt_check(ret);
        ret = fdt_delprop(dtb, chosen, "linux,initrd-end");
        if (ret != -FDT_ERR_NOTFOUND) fdt_check(ret);
    }
    fdt_check(fdt_pack(dtb));
    dtb_size = fdt_totalsize(dtb);
    struct kexec_mod_segment segments[] = {
        {kernel, kernel_size, load, footprint},
        {dtb, dtb_size, dtb_mem, ALIGN_UP(dtb_size, PAGE_SIZE)},
        {initrd, initrd_size, initrd_mem, ALIGN_UP(initrd_size, PAGE_SIZE)}
    };
    struct kexec_mod_load_args args = {
        .entry = load, .nr_segments = initrd ? 3 : 2, .segments = segments, .dtb_mem = dtb_mem
    };
    if (ioctl(fd, KEXEC_MOD_IOCTL_LOAD, &args)) die("load image");
    printf("Image staged at %#lx; DTB at %#lx", load, dtb_mem);
    if (initrd) printf("; initrd at %#lx", initrd_mem);
    puts(". Use --exec to boot it.");
    free(initrd); free(dtb); free(kernel);
    close(fd);
    return EXIT_SUCCESS;
}
