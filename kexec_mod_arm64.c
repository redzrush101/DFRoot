// SPDX-License-Identifier: GPL-2.0-only
/*
 * Standalone Kexec Module - ARM64 Architecture Support
 *
 * Implements ARM64-specific machine_kexec functionality including
 * cache flushing, CPU state preparation, and kernel transition.
 *
 * Based on arch/arm64/kernel/machine_kexec.c
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>
#include <asm/daifflags.h>
#include <asm/memory.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/sysreg.h>
#include <asm/virt.h>

#include "kexec_mod.h"

/* ============================================================
 * External Assembly Symbols
 * ============================================================ */

/* 
 * Relocation code - defined in kexec_mod_relocate.S
 * This code handles MMU disable AND relocation.
 * 
 * Arguments:
 *   x0: el2_switch (unused, pass 0)
 *   x1: physical address of this code (control page phys)
 *   x2: kimage->head (relocation list)
 *   x3: kimage->start (kernel entry point)
 *   x4: dtb_mem (device tree, or 0)
 */
extern const unsigned char kexec_mod_relocate_new_kernel[];
extern const unsigned char kexec_mod_relocate_new_kernel_end[];

/* ============================================================
 * Debug Helpers
 * ============================================================ */

static void kexec_mod_image_info(const struct kexec_mod_image *image)
{
    unsigned long i;

    kexec_mod_info("kexec image info:\n");
    kexec_mod_info("  type:        %d\n", image->type);
    kexec_mod_info("  start:       %lx\n", image->start);
    kexec_mod_info("  head:        %lx\n", image->head);
    kexec_mod_info("  nr_segments: %lu\n", image->nr_segments);

    for (i = 0; i < image->nr_segments; i++) {
        kexec_mod_info("  segment[%lu]: %016lx - %016lx, %lu bytes\n",
                      i,
                      image->segment[i].mem,
                      image->segment[i].mem + image->segment[i].memsz,
                      (unsigned long)image->segment[i].memsz);
    }
}

/* ============================================================
 * Cache Flushing
 * ============================================================ */

static void kexec_mod_list_flush(struct kexec_mod_image *image)
{
    kexec_mod_entry_t *entry;

    for (entry = &image->head; ; entry++) {
        unsigned int flag;
        void *addr;

        kexec_sym___flush_dcache_area(entry, sizeof(kexec_mod_entry_t));

        flag = *entry & IND_FLAGS;
        if (flag == IND_DONE)
            break;

        addr = phys_to_virt(*entry & PAGE_MASK);

        switch (flag) {
        case IND_INDIRECTION:
            entry = (kexec_mod_entry_t *)addr - 1;
            break;
        case IND_SOURCE:
            kexec_sym___flush_dcache_area(addr, PAGE_SIZE);
            break;
        case IND_DESTINATION:
            break;
        default:
            break;
        }
    }
}

static void kexec_mod_segment_flush(const struct kexec_mod_image *image)
{
    unsigned long i;

    for (i = 0; i < image->nr_segments; i++) {
        void *addr = phys_to_virt(image->segment[i].mem);
        kexec_sym___flush_dcache_area(addr, image->segment[i].memsz);
    }
}

/* ============================================================
 * Machine Kexec Implementation
 * ============================================================ */

int kexec_mod_machine_prepare(struct kexec_mod_image *image)
{
    size_t code_size = kexec_mod_relocate_new_kernel_end - kexec_mod_relocate_new_kernel;
    void *code;

    if (PAGE_SHIFT != 12 || !code_size || code_size > PAGE_SIZE)
        return -EOPNOTSUPP;
    image->identity_page = kexec_mod_alloc_control_pages(image, 0);
    if (!image->identity_page)
        return -ENOMEM;
    clear_page(page_address(image->identity_page));
    code = page_address(image->control_code_page);
    memcpy(code, kexec_mod_relocate_new_kernel, code_size);
    kexec_sym___flush_dcache_area(code, code_size);
    flush_icache_range((unsigned long)code, (unsigned long)code + code_size);
    return 0;
}

void kexec_mod_machine_cleanup(struct kexec_mod_image *image)
{
    /* Both pages belong to image->control_pages. */
}

void __attribute__((no_sanitize("cfi"))) kexec_mod_machine_kexec(struct kexec_mod_image *image)
{
    void (*relocate)(unsigned long, unsigned long, unsigned long,
                     unsigned long, unsigned long, unsigned long,
                     unsigned long) = (void *)kexec_mod_relocate_new_kernel;

    /* All allocations were completed during LOAD, before devices and CPUs
     * are stopped. Only local cache operations are allowed at this point. */
    kexec_mod_list_flush(image);
    if (image->head & IND_DONE)
        kexec_mod_segment_flush(image);
    kexec_sym___flush_dcache_area(page_address(image->identity_page), PAGE_SIZE);
    local_daif_mask();
    relocate(0, page_to_phys(image->control_code_page), image->head,
             image->start, image->dtb_mem, page_to_phys(image->identity_page),
             (unsigned long)page_address(image->identity_page));
    BUG();
}

MODULE_LICENSE("GPL");
