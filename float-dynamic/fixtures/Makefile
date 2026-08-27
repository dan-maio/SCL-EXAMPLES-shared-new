# floatlib.so — plain -fPIC ET_DYN for the POSIX dlopen (RAM) loader (Model E).
TC   ?= /home/dan/.local/xPacks/@xpack-dev-tools/arm-none-eabi-gcc/15.2.1-1.1.1/.content/bin
CXX  := $(TC)/arm-none-eabi-g++
MCU  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
PIC  := -fPIC -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E
OPT  := -O0 -ffunction-sections -fdata-sections -std=c++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics
all: build/floatlib.so
build/floatlib.so: src/floatlib.cpp include/floatlib.h
	@mkdir -p build
	$(CXX) $(MCU) $(PIC) $(OPT) -Iinclude -o $@ src/floatlib.cpp
	$(TC)/arm-none-eabi-readelf -l $@ | awk '/LOAD/{print "    "$$0}'
clean:; rm -rf build
