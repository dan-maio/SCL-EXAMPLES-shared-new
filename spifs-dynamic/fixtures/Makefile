# fixtures/Makefile — SPI-flatfs fixture as a PLAIN -fPIC .so (ET_DYN) for the
# POSIX dlopen (RAM) loader (Model E). r10-free (PC-relative GOT); imports the
# STM32 HAL + memcpy/memset from main (resolved via the embedded symtab).
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
READELF := $(TC)/arm-none-eabi-readelf
STRIP := $(TC)/arm-none-eabi-strip
VEN  := ../platform/test/platforms/board-hosted/vendor/common/system/include
FWI  := ../platform/test/platforms/board-hosted/vendor/common/firmware/include
SCL  := ../SCL-shared/common-elf-reader/include
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fPIC -fno-inline-functions -fno-common
INC  := -Iinclude -I$(FWI) -I$(VEN)/stm32f4-hal -I$(VEN)/cmsis -I$(VEN) -I$(SCL)
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
CXXF := $(MCU) $(PIC) -O0 -g3 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
        -fno-threadsafe-statics -fno-use-cxa-atexit $(INC) $(DEF)
B    := build
SRCS := w25q64 spi-shim fs-archive-spi spifs-api
OBJS := $(addprefix $(B)/,$(addsuffix .o,$(SRCS)))

all: $(B)/spifs.so
	@$(READELF) -h $(B)/spifs.so | grep Type | sed 's/^/  /'
$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(CXXF) -c $< -o $@
$(B)/spifs.so: $(OBJS)
	$(CXX) $(CXXF) -shared -nostartfiles -nostdlib -Wl,-E -Wl,--gc-sections \
	    -Wl,--unresolved-symbols=ignore-all $(OBJS) -o $@
	$(STRIP) --strip-debug $@   # 870KB -g3 debug -> ~13KB, so it fits the RAM load buffer
$(B):; mkdir -p $(B)
clean:; rm -rf $(B)
.PHONY: all clean
