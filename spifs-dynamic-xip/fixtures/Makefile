# fixtures/Makefile — SPI-flatfs fixture as an r10-split .so for the XIP dynamic
# loader (scl::xip-dynamic-newlib): -fpic -msingle-pic-base -mpic-register=r10
# -mno-pic-data-is-text-relative, linked into two segments (R-X | RW) by
# xip-split.ld with VA == file_offset, so the R-X text executes from flash.
# Imports the STM32 HAL + memcpy/memset from main (resolved via the embedded symtab).
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
READELF := $(TC)/arm-none-eabi-readelf
STRIP := $(TC)/arm-none-eabi-strip
VEN  := ../platform/test/platforms/board-hosted/vendor/common/system/include
FWI  := ../platform/test/platforms/board-hosted/vendor/common/firmware/include
SCL  := ../SCL-shared/common-elf-reader/include
LD   := xip-split.ld
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative
INC  := -Iinclude -I$(FWI) -I$(VEN)/stm32f4-hal -I$(VEN)/cmsis -I$(VEN) -I$(SCL)
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
CXXF := $(MCU) $(PIC) -O0 -g3 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
        -fno-threadsafe-statics -fno-use-cxa-atexit $(INC) $(DEF)
LDF  := -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic -Wl,--gc-sections
B    := build
SRCS := w25q64 spi-shim fs-archive-spi spifs-api
OBJS := $(addprefix $(B)/,$(addsuffix .o,$(SRCS)))

all: $(B)/spifs.so
$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(CXXF) -c $< -o $@
$(B)/spifs.so: $(OBJS)
	$(CXX) $(CXXF) $(LDF) -T $(LD) $(OBJS) -o $@
	$(STRIP) --strip-debug $@
	@$(READELF) -l $@ | awk '/LOAD/{print "    "$$0}'   # must show R E + RW
$(B):; mkdir -p $(B)
clean:; rm -rf $(B)
.PHONY: all clean
