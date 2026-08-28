# combo-overlay — the W25Q64 SPI-flatfs driver as an overlay module (scl::overlay).
# The 4 driver sources are compiled -fno-pic -mlong-calls and gathered by the
# linker into the shared SRAM window (.ovspifs); main compiles the STM32 HAL the
# driver calls (bound at link time). Same bare NON-PIC template as float-overlay.
cmake_minimum_required(VERSION 3.20)
project(combo-overlay LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=platform/cmake/arm-none-eabi.cmake")
endif()

set(SCL ${CMAKE_SOURCE_DIR}/SCL-shared)
set(PLATFORM ${CMAKE_SOURCE_DIR}/platform)
set(V ${CMAKE_SOURCE_DIR}/vendor)                     # locally-copied STM32 HAL
add_subdirectory(${SCL} scl)                          # -> scl::overlay

set(SCL_PLATFORM "board" CACHE STRING "board or qemu-f411re")
set(PLAT ${PLATFORM}/test/platforms/${SCL_PLATFORM})
set(PLAT_COMMON ${PLATFORM}/test/platforms/common)

set(DRIVER
    fixtures/src/ovfloat.cpp
    fixtures/src/w25q64.cpp fixtures/src/spi-shim.cpp
    fixtures/src/fs-archive-spi.cpp fixtures/src/spifs-api.cpp)

add_executable(combo-overlay
    src/main.cpp src/uart.c
    ${DRIVER}
    ${PLAT}/src/startup.c ${PLAT}/src/semihost.c ${PLAT}/src/board.c
    ${PLAT_COMMON}/src/crt.cpp ${PLAT}/src/libc-shim.c
    # STM32 HAL the driver imports (compiled into main, bound at link time)
    ${V}/stm32f4-hal/stm32f4xx_hal.c ${V}/stm32f4-hal/stm32f4xx_hal_cortex.c
    ${V}/stm32f4-hal/stm32f4xx_hal_gpio.c ${V}/stm32f4-hal/stm32f4xx_hal_rcc.c
    ${V}/stm32f4-hal/stm32f4xx_hal_spi.c ${V}/stm32f4-hal/stm32f4xx_hal_dma.c
    ${V}/stm32f4-hal/stm32f4xx_hal_flash.c ${V}/stm32f4-hal/stm32f4xx_hal_flash_ex.c
    ${V}/stm32f4-hal/stm32f4xx_hal_flash_ramfunc.c
    ${V}/stm32f4-hal/stm32f4xx_hal_pwr.c ${V}/stm32f4-hal/stm32f4xx_hal_pwr_ex.c
    ${V}/cmsis/system_stm32f4xx.c)

target_include_directories(combo-overlay PRIVATE
    src ${PLAT}/include ${SCL}/include
    ${CMAKE_SOURCE_DIR}/fixtures/include
    ${V}/include ${V}/include/stm32f4-hal ${V}/include/cmsis)
target_compile_definitions(combo-overlay PRIVATE
    SCL_BOARD_WEACT=1 HSE_VALUE=25000000 STM32F411xE USE_HAL_DRIVER)
target_link_libraries(combo-overlay PRIVATE scl::overlay)

# The driver runs in the SRAM window: NON-PIC + -mlong-calls (calls main's HAL
# across >Thumb-BL reach). No -ffunction-sections so each object has one .text/
# .rodata/.data the linker gathers whole into .ovspifs.
set_source_files_properties(${DRIVER}
    PROPERTIES COMPILE_OPTIONS "-fno-pic;-mlong-calls;-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit")

# Ordinary NON-PIC firmware.
target_compile_options(combo-overlay PRIVATE
    -fno-pic -fno-pie -Os
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics> $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>)

separate_arguments(_mcu UNIX_COMMAND "${SCL_MCU_FLAGS}")
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_mcu} -print-file-name=libm.a
                OUTPUT_VARIABLE _libm OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_mcu} -print-file-name=libgcc.a
                OUTPUT_VARIABLE _libgcc OUTPUT_STRIP_TRAILING_WHITESPACE)
target_link_options(combo-overlay PRIVATE
    -T${CMAKE_SOURCE_DIR}/src/sections-overlay.ld
    -nostdlib -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
    -Wl,-Map=$<TARGET_FILE:combo-overlay>.map)
target_link_libraries(combo-overlay PRIVATE ${_libm} ${_libgcc})

add_custom_command(TARGET combo-overlay POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:combo-overlay> $<TARGET_FILE:combo-overlay>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:combo-overlay> $<TARGET_FILE:combo-overlay>.bin)
