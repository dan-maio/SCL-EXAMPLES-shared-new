# fixtures/Makefile — build floatlib as an ET_REL module (doc import_in_project_full §A.1).
#   compile -mlong-calls -fno-pic, then a partial link `ld -r` to keep it ET_REL.
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
LD   := $(TC)/arm-none-eabi-ld
READELF := $(TC)/arm-none-eabi-readelf
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
REL  := -mlong-calls -fno-pic
CXXF := $(MCU) $(REL) -Os -ffreestanding -fno-common -std=c++20 \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit
B := build

all: $(B)/floatlib.rel.o
	@echo "== fixtures: floatlib.rel.o (ET_REL) =="

$(B)/floatlib.rel.o: src/floatlib.cpp | $(B)
	$(CXX) $(CXXF) -Iinclude -c $< -o $(B)/floatlib.o
	$(LD) -r $(B)/floatlib.o -o $@
	@$(READELF) -h $@ | grep -q 'REL (Relocatable' && echo "  [ET_REL] $@"

$(B):
	mkdir -p $(B)
clean:
	rm -rf $(B)
.PHONY: all clean
