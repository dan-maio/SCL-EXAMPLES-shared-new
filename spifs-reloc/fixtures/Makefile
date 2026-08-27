# fixtures/Makefile — build the SPI-flatfs fixture as an ET_REL module.
#   w25q64 (HAL driver) + spi-shim + fs-archive-spi (flatfs) + spifs-api (C ABI)
#   compiled -mlong-calls -fno-pic, partial-linked (ld -r) into spifs.rel.o.
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
LD   := $(TC)/arm-none-eabi-ld
READELF := $(TC)/arm-none-eabi-readelf
V    := ../vendor
SCL  := ../relocatable-shared/common-elf-reader/include
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
REL  := -mlong-calls -fno-pic
INC  := -Iinclude -I$(V)/include -I$(V)/include/stm32f4-hal -I$(V)/include/cmsis -I$(SCL)
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
CXXF := $(MCU) $(REL) -Os -ffreestanding -fno-common -std=c++20 \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit $(INC) $(DEF)
B    := build
SRCS := w25q64 spi-shim fs-archive-spi spifs-api
OBJS := $(addprefix $(B)/,$(addsuffix .o,$(SRCS)))

all: $(B)/spifs.rel.o
	@echo "== fixture: spifs.rel.o (ET_REL, SPI flatfs driver) =="

$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(CXXF) -c $< -o $@

$(B)/spifs.rel.o: $(OBJS)
	$(LD) -r $(OBJS) -o $@
	@$(READELF) -h $@ | grep -q 'REL (Relocatable' && echo "  [ET_REL] $@"

$(B):
	mkdir -p $(B)
clean:
	rm -rf $(B)
.PHONY: all clean
