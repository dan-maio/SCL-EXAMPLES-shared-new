# fixtures/Makefile — combo-dynamic builds BOTH modules as plain -fPIC .so
# (ET_DYN) for the POSIX dlopen (RAM) loader (Model E):
#   floatlib.so  — float_add / float_mul
#   spifs.so     — W25Q64 driver + flat-fs (imports the STM32 HAL from main)
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
READELF := $(TC)/arm-none-eabi-readelf
STRIP := $(TC)/arm-none-eabi-strip
VEN  := ../platform/test/platforms/board-hosted/vendor/common/system/include
FWI  := ../platform/test/platforms/board-hosted/vendor/common/firmware/include
SCL  := ../SCL-shared/common-elf-reader/include
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fPIC -fno-inline-functions -fno-common
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
COMMON := $(MCU) $(PIC) -O0 -g3 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
          -fno-threadsafe-statics -fno-use-cxa-atexit
SPI_INC := -Iinclude -I$(FWI) -I$(VEN)/stm32f4-hal -I$(VEN)/cmsis -I$(VEN) -I$(SCL)
LDF  := -shared -nostartfiles -nostdlib -Wl,-E -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all
B    := build
SPI_SRCS := $(addprefix $(B)/,w25q64.o spi-shim.o fs-archive-spi.o spifs-api.o)

all: $(B)/floatlib.so $(B)/spifs.so
	@$(READELF) -h $(B)/floatlib.so | grep Type | sed 's/^/  floatlib /'
	@$(READELF) -h $(B)/spifs.so    | grep Type | sed 's/^/  spifs    /'

$(B)/floatlib.so: src/floatlib.cpp include/floatlib.h | $(B)
	$(CXX) $(COMMON) $(DEF) -Iinclude $(LDF) -o $@ src/floatlib.cpp

$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(COMMON) $(DEF) $(SPI_INC) -c $< -o $@
$(B)/spifs.so: $(SPI_SRCS)
	$(CXX) $(COMMON) $(DEF) $(SPI_INC) $(LDF) $(SPI_SRCS) -o $@
	$(STRIP) --strip-debug $@   # keep the RAM load buffer small

$(B):; mkdir -p $(B)
clean:; rm -rf $(B)
.PHONY: all clean
