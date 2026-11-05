CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror
FDT := ../third_party/dtc/libfdt
OUT := ../build
FDT_SRCS := $(addprefix $(FDT)/,fdt.c fdt_ro.c fdt_wip.c fdt_sw.c fdt_rw.c \
            fdt_strerror.c fdt_empty_tree.c fdt_addresses.c fdt_overlay.c fdt_check.c)

.PHONY: all clean
all: $(OUT)/kexec-load $(OUT)/kexec-selftest

$(OUT):
	mkdir -p $@

$(OUT)/kexec-load: kexec_mod_load.c ../kexec_mod_user.h $(FDT_SRCS) | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(FDT) $< $(FDT_SRCS) $(LDFLAGS) -o $@

$(OUT)/kexec-selftest: kexec-selftest.c ../kexec_mod_user.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LDFLAGS) -o $@

clean:
	rm -f $(OUT)/kexec-load $(OUT)/kexec-selftest
