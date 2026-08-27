# fixtures/Makefile — SPI-flatfs fixture as a PLAIN -fPIC .so (ET_DYN), copied
# WHOLE into RAM and run there by the static loader (scl::static). r10-free
# (PC-relative GOT); no single-pic-base, no split, no windows.
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
READELF := $(TC)/arm-none-eabi-readelf
ST   := ../platform/test-static/test-static
SCL  := ../SCL-shared/common-elf-reader/include
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fPIC -fno-inline-functions -fno-common
INC  := -Iinclude -I$(ST)/include -I$(ST)/system/include/stm32f4-hal \
        -I$(ST)/system/include/cmsis -I$(ST)/system/include -I$(SCL)
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
CXXF := $(MCU) $(PIC) -O0 -g3 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
        -fno-threadsafe-statics -fno-use-cxa-atexit $(INC) $(DEF)
B    := build
SRCS := w25q64 spi-shim fs-archive-spi spifs-api
OBJS := $(addprefix $(B)/,$(addsuffix .o,$(SRCS)))

all: $(B)/spifs.so $(B)/floatlib.so
	@echo "== fixture: spifs.so (plain -fPIC ET_DYN, full-in-RAM) =="
	@$(READELF) -h $(B)/spifs.so | grep Type | sed 's/^/  /'

$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(CXXF) -c $< -o $@
$(B)/spifs.so: $(OBJS)
	$(CXX) $(CXXF) -shared -nostartfiles -nostdlib -Wl,-E -Wl,--gc-sections \
	    -Wl,--unresolved-symbols=ignore-all $(OBJS) -o $@
$(B):
	mkdir -p $(B)
clean:
	rm -rf $(B)
.PHONY: all clean

$(B)/floatlib.o: src/floatlib.cpp | $(B)
	$(CXX) $(CXXF) -c $< -o $@
$(B)/floatlib.so: $(B)/floatlib.o
	$(CXX) $(CXXF) -shared -nostartfiles -nostdlib -Wl,-E -Wl,--gc-sections \
	    -Wl,--unresolved-symbols=ignore-all $< -o $@
