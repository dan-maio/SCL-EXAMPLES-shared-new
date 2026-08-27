# fixtures/Makefile — combo-dynamic-xip builds BOTH modules as r10-split .so for
# the XIP dynamic loader (scl::xip-dynamic-newlib): -fpic -msingle-pic-base
# -mpic-register=r10 -mno-pic-data-is-text-relative, linked R-X | RW by
# xip-split.ld so the text executes from flash.
#   floatlib.so  — float_add / float_mul
#   spifs.so     — W25Q64 driver + flat-fs (imports the STM32 HAL from main)
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
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
COMMON := $(MCU) $(PIC) -O0 -g3 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
          -fno-threadsafe-statics -fno-use-cxa-atexit
SPI_INC := -Iinclude -I$(FWI) -I$(VEN)/stm32f4-hal -I$(VEN)/cmsis -I$(VEN) -I$(SCL)
LDF  := -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic -Wl,--gc-sections -T $(LD)
B    := build
SPI_SRCS := $(addprefix $(B)/,w25q64.o spi-shim.o fs-archive-spi.o spifs-api.o)

all: $(B)/floatlib.so $(B)/spifs.so
	@$(READELF) -l $(B)/spifs.so | awk '/LOAD/{print "    spifs "$$0}'   # must show R E + RW

$(B)/floatlib.so: src/floatlib.cpp include/floatlib.h | $(B)
	$(CXX) $(COMMON) $(DEF) -Iinclude $(LDF) -o $@ src/floatlib.cpp
	$(STRIP) --strip-debug $@

$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(COMMON) $(DEF) $(SPI_INC) -c $< -o $@
$(B)/spifs.so: $(SPI_SRCS)
	$(CXX) $(COMMON) $(DEF) $(SPI_INC) $(LDF) $(SPI_SRCS) -o $@
	$(STRIP) --strip-debug $@

$(B):; mkdir -p $(B)
clean:; rm -rf $(B)
.PHONY: all clean
