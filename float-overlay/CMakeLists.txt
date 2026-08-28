# float-overlay — overlay loading (scl::overlay): two modules time-share one SRAM
# window. Same template as float-reloc (bare, NON-PIC firmware) + the overlay
# linker script; the "modules" are built WITH the firmware (fixtures/src/ov*.c),
# placed by the linker in the shared window, and "loaded" with a memcpy.
cmake_minimum_required(VERSION 3.20)
project(float-overlay LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=platform/cmake/arm-none-eabi.cmake")
endif()

set(SCL ${CMAKE_SOURCE_DIR}/SCL-shared)
set(PLATFORM ${CMAKE_SOURCE_DIR}/platform)
add_subdirectory(${SCL} scl)                          # -> scl::overlay

set(SCL_PLATFORM "board" CACHE STRING "board or qemu-f411re")
set(PLAT ${PLATFORM}/test/platforms/${SCL_PLATFORM})
set(PLAT_COMMON ${PLATFORM}/test/platforms/common)
if(SCL_PLATFORM STREQUAL "qemu-f411re")
    set(BOARD_DEFS "")
else()
    set(BOARD_DEFS SCL_BOARD_WEACT=1 HSE_VALUE=25000000)
endif()

add_executable(float-overlay
    src/main.cpp
    src/uart.c
    fixtures/src/ovadd.c fixtures/src/ovmul.c          # the overlay modules
    ${PLAT}/src/startup.c ${PLAT}/src/semihost.c ${PLAT}/src/board.c
    ${PLAT_COMMON}/src/crt.cpp ${PLAT}/src/libc-shim.c)
target_include_directories(float-overlay PRIVATE src ${PLAT}/include ${SCL}/include)
target_compile_definitions(float-overlay PRIVATE ${BOARD_DEFS})
target_link_libraries(float-overlay PRIVATE scl::overlay)

# The overlay modules are NON-PIC + -mlong-calls (they run in the SRAM window but
# call across >Thumb-BL reach; -fno-pic overrides the toolchain's global -fpic).
set_source_files_properties(fixtures/src/ovadd.c fixtures/src/ovmul.c
    PROPERTIES COMPILE_OPTIONS "-fno-pic;-mlong-calls")

# Ordinary NON-PIC firmware (like float-reloc).
target_compile_options(float-overlay PRIVATE
    -fno-pic -fno-pie -Os -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics> $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>)

separate_arguments(_mcu UNIX_COMMAND "${SCL_MCU_FLAGS}")
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_mcu} -print-file-name=libm.a
                OUTPUT_VARIABLE _libm OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_mcu} -print-file-name=libgcc.a
                OUTPUT_VARIABLE _libgcc OUTPUT_STRIP_TRAILING_WHITESPACE)
target_link_options(float-overlay PRIVATE
    -T${CMAKE_SOURCE_DIR}/src/sections-overlay.ld
    -nostdlib -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
    -Wl,-Map=$<TARGET_FILE:float-overlay>.map)
target_link_libraries(float-overlay PRIVATE ${_libm} ${_libgcc})

add_custom_command(TARGET float-overlay POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:float-overlay> $<TARGET_FILE:float-overlay>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:float-overlay> $<TARGET_FILE:float-overlay>.bin)
