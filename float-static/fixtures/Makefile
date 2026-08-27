# fixtures/Makefile — build floatlib.so as an r10-based PIC XIP library
# (doc import_in_project_full §B.1): text executes from flash, only the RW
# segment goes to RAM (r10 = its GOT base). Split into R-X | RW segments.
#
#   NOTE the -nostdlib -nodefaultlibs: without them the default startfiles/libs
#   merge text+data into one RWE segment and the XIP loader rejects it (§B gotcha).
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
READELF := $(TC)/arm-none-eabi-readelf
LD   := ../static-shared/test-xip/fixtures-src/ldscripts/xip-split.ld
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative
CXXF := $(MCU) $(PIC) -O0 -ffunction-sections -fdata-sections -std=c++20 \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
LDFLAGS := -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic -Wl,--gc-sections
B := build

all: $(B)/floatlib.so
	@echo "== fixtures: floatlib.so (r10 PIC, split R-X|RW) =="
	@$(READELF) -l $(B)/floatlib.so | awk '/LOAD/{print "  "$$0}'

$(B)/floatlib.so: src/floatlib.cpp | $(B)
	$(CXX) $(CXXF) $(LDFLAGS) -T $(LD) -Iinclude $< -o $@

$(B):
	mkdir -p $(B)
clean:
	rm -rf $(B)
.PHONY: all clean
