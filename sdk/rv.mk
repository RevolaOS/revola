# SPDX-License-Identifier: GPL-3.0-only
# Builds one extension: every module in MODULES (main first) becomes <name>.rvx
# and manifest.toml becomes manifest.bin, all in OUT.
#
#   ID := hello                  # the extension id, as in manifest.toml
#   MODULES := main              # one .rvx per module; sources in <module>/ or
#   main_SOURCES := main.c       # listed per module
#   include $(SDK)/rv.mk

SDK ?= $(dir $(lastword $(MAKEFILE_LIST)))
CROSS_COMPILE ?= arm-elf-eabi-
OUT ?= build
include $(SDK)/rv_firmware.mk

CC := $(CROSS_COMPILE)gcc
CFLAGS := -mcpu=arm926ej-s -mthumb -mthumb-interwork -mlong-calls -Os -std=c11 \
    -ffreestanding -fno-builtin -fno-common -fno-pic -fno-jump-tables \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
    -I$(SDK) $(EXTRA_CFLAGS)
LDFLAGS := -nostdlib -Wl,-T,$(SDK)/rvx.lds -Wl,--emit-relocs \
    -Wl,--just-symbols=$(SDK)/$(RV_FIRMWARE_LD) -lgcc

define module_rules
$(OUT)/$(1).rvx: $(OUT)/$(1).elf
	python3 $(SDK)/rvx-pack $$< $$@ --prefix $(CROSS_COMPILE) --api $(RV_API) \
	    --firmware $(RV_FIRMWARE_ID) --module $(ID)/$(1)
$(OUT)/$(1).elf: $$($(1)_SOURCES) $(SDK)/rvx.lds | $(OUT)
	$(CC) $(CFLAGS) $$(filter %.c %.S,$$^) $(LDFLAGS) -Wl,-Map,$$(@:.elf=.map) -o $$@
endef
$(foreach module,$(MODULES),$(eval $(call module_rules,$(module))))

all: $(addprefix $(OUT)/,$(addsuffix .rvx,$(MODULES))) $(OUT)/manifest.bin

$(OUT)/manifest.bin: manifest.toml | $(OUT)
	python3 $(SDK)/manifest $< $@

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
.DEFAULT_GOAL := all
