# SPDX-License-Identifier: GPL-3.0-only
# Builds the eApp runtime for one app and packs the app into a package folder
# OUT/<ID>/ that goes to /Revola/Games/<ID>/ or /Revola/Apps/<ID>/.
#
#   ID := hello                  # the package id, rv_app_application.package_id
#   NAME := Hello                # what Extras lists
#   KIND := game                 # game (under /Revola/Games/) or app (/Revola/Apps/)
#   SOURCES := main.c            # the app's sources
#   ICON := icon.png             # 55 x 55 PNG shown in the list
#   HEAP_KIB := 5120             # the heap the host gives the app
#   include $(SDK)/app/rv_app.mk
#
# An app with its own link step (newlib, extra libraries) uses the variables
# instead of the default rules: RV_APP_ARCH_FLAGS, RV_APP_CFLAGS, RV_APP_LDFLAGS,
# RV_APP_STARTUP and RV_APP_RUNTIME, the targets $(RV_APP_STARTUP) and
# $(RV_APP_RUNTIME), and `python3 $(SDK)/app/eapp-pack`.

SDK ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CROSS_COMPILE ?= arm-elf-eabi-
OUT ?= build
KIND ?= game
HEAP_KIB ?= 5120
include $(SDK)/rv_firmware.mk

ifeq ($(filter game app,$(KIND)),)
$(error KIND must be game or app)
endif
$(if $(ID),,$(error ID names the package folder))
$(if $(NAME),,$(error NAME is what Extras lists))
$(if $(ICON),,$(error ICON names a 55 x 55 PNG))

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
RV_APP_ARCH_FLAGS := -mcpu=arm926ej-s -marm -mfloat-abi=soft
# Apple's code is 256 MiB away from the image, out of reach of a plain BL.
RV_APP_CFLAGS := -std=c11 -Os $(RV_APP_ARCH_FLAGS) -mlong-calls \
    -ffreestanding -fno-builtin -fno-common -fno-stack-protector \
    -fno-pic -fno-pie -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
    -I$(SDK)/app -I$(SDK) $(EXTRA_CFLAGS)
RV_APP_LDFLAGS := -nostdlib -Wl,-T,$(SDK)/app/runtime/rv_app.lds -Wl,--gc-sections \
    -Wl,--just-symbols=$(SDK)/$(RV_FIRMWARE_LD)
RV_APP_RUNTIME_DIR := $(OUT)/runtime
RV_APP_RUNTIME := $(RV_APP_RUNTIME_DIR)/librv_app.a
RV_APP_STARTUP := $(RV_APP_RUNTIME_DIR)/startup.o
RV_APP_RUNTIME_OBJECTS := $(addprefix $(RV_APP_RUNTIME_DIR)/,\
    runtime.o imports.o memory.o time.o input.o files.o directory.o display.o text.o ui.o wheel.o)

$(RV_APP_RUNTIME): $(RV_APP_RUNTIME_OBJECTS)
	$(RM) $@
	$(AR) rcs $@ $^

$(RV_APP_RUNTIME_DIR)/%.o: $(SDK)/app/runtime/%.c $(SDK)/app/rv_app.mk
	@mkdir -p $(@D)
	$(CC) $(RV_APP_CFLAGS) -MMD -MP -c $< -o $@

# The interface ids and counts the import list names, in a form the assembler reads.
$(RV_APP_RUNTIME_DIR)/imports_ids.h: $(SDK)/$(RV_FIRMWARE_LD:.ld=.h) $(SDK)/app/rv_app.mk
	@mkdir -p $(@D)
	grep -E '^#define (GameAPI_[A-Za-z]+_(Id[0-3]|Count)|GameAPIModule_NameBytes|GameImport_Tail) ' $< > $@

$(RV_APP_RUNTIME_DIR)/imports.o: $(SDK)/app/runtime/imports.S $(SDK)/app/runtime/imports.def \
		$(RV_APP_RUNTIME_DIR)/imports_ids.h $(SDK)/app/rv_app.mk
	@mkdir -p $(@D)
	$(CC) $(RV_APP_ARCH_FLAGS) -I$(@D) -I$(SDK)/app/runtime -c $< -o $@

$(RV_APP_STARTUP): $(SDK)/app/runtime/startup.S $(SDK)/app/rv_app.mk
	@mkdir -p $(@D)
	$(CC) $(RV_APP_ARCH_FLAGS) -c $< -o $@

-include $(RV_APP_RUNTIME_OBJECTS:.o=.d)

# The default rules for an app of plain C sources without a C library.
RV_APP_OBJECTS := $(patsubst %,$(OUT)/app/%.o,$(SOURCES))

$(OUT)/app/%.o: %
	@mkdir -p $(dir $@)
	$(CC) $(RV_APP_CFLAGS) -MMD -MP -c $< -o $@
-include $(RV_APP_OBJECTS:.o=.d)

$(OUT)/$(ID).elf: $(RV_APP_STARTUP) $(RV_APP_OBJECTS) $(RV_APP_RUNTIME) $(SDK)/app/runtime/rv_app.lds
	$(CC) $(RV_APP_ARCH_FLAGS) $(RV_APP_STARTUP) $(RV_APP_OBJECTS) $(RV_APP_RUNTIME) -lgcc \
	    $(RV_APP_LDFLAGS) -Wl,-Map,$(@:.elf=.map) -o $@

$(OUT)/$(ID).eapp: $(OUT)/$(ID).elf
	$(OBJCOPY) -O binary $< $@

all: $(OUT)/$(ID)/Manifest.plist

$(OUT)/$(ID)/Manifest.plist: $(OUT)/$(ID).eapp $(ICON) $(FILES)
	python3 $(SDK)/app/eapp-pack --kind $(KIND) --id $(ID) --name "$(NAME)" \
	    --image $< --icon $(ICON) --symbols $(SDK)/$(RV_FIRMWARE_LD:.ld=.h) \
	    --heap-kib $(HEAP_KIB) --out $(OUT) $(FILES)

clean:
	rm -rf $(OUT)

.PHONY: all clean
.DEFAULT_GOAL := all
