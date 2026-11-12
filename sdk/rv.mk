# SPDX-License-Identifier: GPL-3.0-only
# Builds one extension into OUT/<ID>.rvx: manifest.toml, every module in
# MODULES, in the manifest's order (main first), and the manifest's assets.
#
#   ID := hello                  # the extension id, as in manifest.toml
#   MODULES := main              # sources in <module>/ or
#   main_SOURCES := main.c       # listed per module
#   main_ARM_SOURCES := dsp.c    # optional: compiled as ARM, not Thumb
#   ASSETS := $(wildcard assets/*)  # the default: asset files, rebuilt on change
#   include $(SDK)/rv.mk

SDK ?= $(dir $(lastword $(MAKEFILE_LIST)))
CROSS_COMPILE ?= arm-elf-eabi-
OUT ?= build
ASSETS ?= $(wildcard assets/*)
include $(SDK)/rv_firmware.mk
SDK_SOURCES := rv_libc.c rv_math.c rv_canvas.c
SDK_OBJECTS := $(patsubst %.c,$(OUT)/%.o,$(SDK_SOURCES))

CC := $(CROSS_COMPILE)gcc
CFLAGS := -mcpu=arm926ej-s -mthumb -mthumb-interwork -mlong-calls -Os -std=c11 \
    -ffreestanding -fno-builtin -fno-common -fno-pic -fno-jump-tables \
    -fno-unwind-tables -fno-asynchronous-unwind-tables -Wall -Wextra -Werror \
    -Wno-missing-field-initializers \
    -I$(SDK) $(EXTRA_CFLAGS)
ARM_CFLAGS := $(filter-out -mthumb,$(CFLAGS)) -marm -fno-optimize-sibling-calls
LDFLAGS := -nostdlib -Wl,-T,$(SDK)/rvx.lds -Wl,--emit-relocs -Wl,--gc-sections \
    -Wl,--just-symbols=$(SDK)/$(RV_FIRMWARE_LD) -lgcc

# One object per source, with the headers it includes as dependencies.
define module_rules
$(1)_OBJECTS := $$(patsubst %,$(OUT)/$(1)/%.o,$$($(1)_SOURCES)) \
    $$(patsubst %,$(OUT)/$(1)-arm/%.o,$$($(1)_ARM_SOURCES))
$(OUT)/$(1).elf: $$($(1)_OBJECTS) $(SDK_OBJECTS) $(SDK)/rvx.lds | $(OUT)
	$(CC) $(CFLAGS) $$(filter %.o,$$^) $(LDFLAGS) -Wl,-Map,$$(@:.elf=.map) -o $$@
$(OUT)/$(1)/%.o: %
	@mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) -MMD -MP -c $$< -o $$@
$(OUT)/$(1)-arm/%.o: %
	@mkdir -p $$(dir $$@)
	$(CC) $(ARM_CFLAGS) -MMD -MP -c $$< -o $$@
-include $$($(1)_OBJECTS:.o=.d)
endef
$(foreach module,$(MODULES),$(eval $(call module_rules,$(module))))

# The SDK's own sources go into every module, each function and object in its
# own section, so --gc-sections keeps only those used.
define sdk_rules
$(OUT)/$(1).o: $(SDK)/$(1).c | $(OUT)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -fno-tree-loop-distribute-patterns \
	    -MMD -MP -c $$< -o $$@
-include $(OUT)/$(1).d
endef
$(foreach source,$(SDK_SOURCES),$(eval $(call sdk_rules,$(basename $(source)))))

all: $(OUT)/$(ID).rvx

$(OUT)/$(ID).rvx: manifest.toml $(addprefix $(OUT)/,$(addsuffix .elf,$(MODULES))) $(ASSETS)
	python3 $(SDK)/rvx-pack $@ $< $(filter %.elf,$^) --prefix $(CROSS_COMPILE) \
	    --api $(RV_API) --firmware $(RV_FIRMWARE_ID)

$(OUT):
	mkdir -p $@

clean:
	rm -rf $(OUT)

.PHONY: all clean
.DEFAULT_GOAL := all
