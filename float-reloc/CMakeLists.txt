# float-reloc — RAM-execution float app (doc import_in_project_full §A).
# Follows the guide: SCL imported from git as a submodule, consumed via
# add_subdirectory + scl::reloc; ordinary NON-PIC firmware; plain linker script.
cmake_minimum_required(VERSION 3.20)
project(float-reloc LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=relocatable-shared/cmake/arm-none-eabi.cmake")
endif()

set(SCL ${CMAKE_SOURCE_DIR}/relocatable-shared)
add_subdirectory(${SCL} scl)                          # defines scl::reloc (INTERFACE source lib)

# platform selection (doc §0): board (weact) or qemu-f411re
set(SCL_PLATFORM "board" CACHE STRING "board or qemu-f411re")
set(PLAT ${SCL}/test/platforms/${SCL_PLATFORM})
set(PLAT_COMMON ${SCL}/test/platforms/common)
if(SCL_PLATFORM STREQUAL "qemu-f411re")
    set(BOARD_DEFS "")
else()
    set(BOARD_DEFS SCL_BOARD_WEACT=1 HSE_VALUE=25000000)
endif()

add_executable(float-reloc
    src/main.cpp
    ${PLAT}/src/startup.c ${PLAT}/src/semihost.c ${PLAT}/src/board.c
    ${PLAT_COMMON}/src/crt.cpp ${PLAT}/src/libc-shim.c)
target_include_directories(float-reloc PRIVATE ${PLAT}/include ${SCL}/include)
target_compile_definitions(float-reloc PRIVATE ${BOARD_DEFS}
    RELOC_LIB_DIR="${CMAKE_SOURCE_DIR}/fixtures/build")
target_link_libraries(float-reloc PRIVATE scl::reloc)     # <- the loader, as INTERFACE

# ordinary NON-PIC firmware: override the toolchain's -fpic; no -pie at link
target_compile_options(float-reloc PRIVATE
    -fno-pic -fno-pie -Os -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics> $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>)

separate_arguments(_mcu UNIX_COMMAND "${SCL_MCU_FLAGS}")
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_mcu} -print-file-name=libm.a
                OUTPUT_VARIABLE _libm OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_C_COMPILER} ${_mcu} -print-file-name=libgcc.a
                OUTPUT_VARIABLE _libgcc OUTPUT_STRIP_TRAILING_WHITESPACE)
target_link_options(float-reloc PRIVATE
    -T${CMAKE_SOURCE_DIR}/src/plain.ld
    -nostdlib -nostartfiles -Wl,--gc-sections -Wl,--no-warn-rwx-segments
    -Wl,-Map=$<TARGET_FILE:float-reloc>.map)
target_link_libraries(float-reloc PRIVATE ${_libm} ${_libgcc})

add_custom_command(TARGET float-reloc POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:float-reloc> $<TARGET_FILE:float-reloc>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:float-reloc> $<TARGET_FILE:float-reloc>.bin)
