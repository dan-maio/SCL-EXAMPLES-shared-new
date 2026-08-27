# fixtures/Makefile — SPI-flatfs fixture as an r10-based PIC XIP .so (split R-X|RW).
#   w25q64 (HAL driver) + spi-shim + fs-archive-spi (flatfs) + spifs-api (C ABI)
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
READELF := $(TC)/arm-none-eabi-readelf
SU   := ../static-shared/test/platforms/board-hosted/vendor
SCL  := ../static-shared/common-elf-reader/include
LD   := ../static-shared/test-xip/fixtures-src/ldscripts/xip-split.ld
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative
INC  := -Iinclude -I$(SU)/common/firmware/include -I$(SU)/common/system/include/stm32f4-hal \
        -I$(SU)/common/system/include/cmsis -I$(SU)/common/system/include -I$(SCL)
DEF  := -DSTM32F411xE -DUSE_HAL_DRIVER
CXXF := $(MCU) $(PIC) -O0 -ffunction-sections -fdata-sections -std=c++20 \
        -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit $(INC) $(DEF)
LDF  := -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic \
        -Wl,--gc-sections -Wl,--unresolved-symbols=ignore-all
B    := build
SRCS := w25q64 spi-shim fs-archive-spi spifs-api
OBJS := $(addprefix $(B)/,$(addsuffix .o,$(SRCS)))

all: $(B)/spifs.so
	@echo "== fixture: spifs.so (r10 PIC, split R-X|RW) =="
	@$(READELF) -l $(B)/spifs.so | awk '/LOAD/{print "  "$$0}'

$(B)/%.o: src/%.cpp | $(B)
	$(CXX) $(CXXF) -c $< -o $@
$(B)/spifs.so: $(OBJS)
	$(CXX) $(CXXF) $(LDF) -T $(LD) $(OBJS) -o $@
$(B):
	mkdir -p $(B)
clean:
	rm -rf $(B)
.PHONY: all clean
