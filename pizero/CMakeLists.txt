# pizero — one small app per SCL loader, on the Pi-Zero RP2350B under the µOS++ III SMP kernel.
# Each loads fixtures/floatlib.cpp at boot on core 0, then calls it from both cores.
# Board support comes from the port repository. Run with ./run-all-pizero.sh.
cmake_minimum_required(VERSION 3.20)

set(UOS_PORT_DIR "/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii-cortexm"
    CACHE PATH "micro-os-plus-iii-cortexm working copy")
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${UOS_PORT_DIR}/../micro-os-plus-iii/cmake/toolchains/arm-none-eabi.cmake")
endif()

project(scl-pizero-examples LANGUAGES C CXX ASM)

set(BOARD "pico2-pizero" CACHE STRING "" FORCE)
add_subdirectory("${UOS_PORT_DIR}" uos)

set(SCL_HOSTED ON CACHE BOOL "" FORCE)     # scl::dynamic-newlib and its XIP form need it
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared" scl)

set(UOS_PICO2 "${UOS_PORT_DIR}/test/boards/pico2")
set(BOARD_SRCS "${UOS_PICO2}/glue/uart.cpp" "${UOS_PICO2}/glue/multicore.cpp" "${UOS_PICO2}/glue/rtos-glue.cpp"
    "${UOS_PICO2}/glue/trace-uart.cpp" "${UOS_PORT_DIR}/test/boards/shared/syscalls.c")
set(BOOT_LD "${UOS_PORT_DIR}/test/pico2-pizero/psram-mat-test-250/psram-sram-boot.ld")

# ---- floatlib, in each form a loader takes -----------------------------------------
set(LIB_SRC "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/floatlib.cpp")
set(LDS "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts")
set(FIX "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
file(MAKE_DIRECTORY "${FIX}")
set(MCU -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16)
set(LIB_CXX -O1 -std=c++20 -ffreestanding -fno-exceptions -fno-rtti)
set(SO_LINK -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E)
# scl::reloc: an ET_REL object.
add_custom_command(OUTPUT "${FIX}/floatlib.rel.o"
    COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -mlong-calls -fno-pic -r -nostdlib "${LIB_SRC}" -o "${FIX}/floatlib.rel.o"
    DEPENDS "${LIB_SRC}" VERBATIM)
# scl::dynamic-nolib and the XIP loaders: r10 single-pic-base, with text and data split.
add_custom_command(OUTPUT "${FIX}/floatlib.so"
    COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -mlong-calls -fpic -msingle-pic-base -mpic-register=r10
            -mno-pic-data-is-text-relative ${SO_LINK} -Wl,-Bsymbolic -T "${LDS}/xip-split.ld" "${LIB_SRC}" -o "${FIX}/floatlib.so"
    DEPENDS "${LIB_SRC}" "${LDS}/xip-split.ld" VERBATIM)
# scl::static and scl::dynamic-newlib: a plain -fPIC .so.
add_custom_command(OUTPUT "${FIX}/floatlib.pic.so"
    COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -fPIC ${SO_LINK} "${LIB_SRC}" -o "${FIX}/floatlib.pic.so"
    DEPENDS "${LIB_SRC}" VERBATIM)
# scl::optimal: -fno-pic, linked at the fixed windows in common/arena.S.
add_custom_command(OUTPUT "${FIX}/floatlib.opt.so"
    COMMAND sed -e "s/__TEXT_BASE__/0x20070000/g" -e "s/__DATA_BASE__/0x20078000/g" "${LDS}/nopic-split.ld" > "${FIX}/floatlib.opt.ld"
    COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -fno-pic -mlong-calls ${SO_LINK} -Wl,-z,max-page-size=0x100
            -Wl,--unresolved-symbols=ignore-all -T "${FIX}/floatlib.opt.ld" "${LIB_SRC}" -o "${FIX}/floatlib.opt.so"
    DEPENDS "${LIB_SRC}" "${LDS}/nopic-split.ld")
add_custom_target(floatlib DEPENDS "${FIX}/floatlib.rel.o" "${FIX}/floatlib.so" "${FIX}/floatlib.pic.so" "${FIX}/floatlib.opt.so")

# ---- one app per loader -------------------------------------------------------------
function(pizero_example name target)
    cmake_parse_arguments(A "" "LD" "SRCS" ${ARGN})
    if(NOT A_LD)
        set(A_LD "${BOOT_LD}")
    endif()
    uos_add_app("${name}-hwd"
        SOURCES ${name}/main.cpp common/pizero.cpp common/arena.S ${A_SRCS} ${BOARD_SRCS}
        NCPU 2
        LINKER_SCRIPT "${A_LD}"
        DEFINES TRACE SEMIHOST HW_BUILD EXAMPLE_FIXTURE_DIR="${FIX}"
        INCLUDES "${UOS_PORT_DIR}/test/boards/shared" "${CMAKE_CURRENT_SOURCE_DIR}/common"
        LIBRARIES micro-os-plus::cortexm ${target})
    add_dependencies("${name}-hwd" floatlib)
    target_link_options("${name}-hwd" PRIVATE -Wl,--wrap=main)   # load after constructors, before the kernel
    set_target_properties("${name}-hwd" PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
    list(APPEND EXAMPLES "${name}-hwd")
    set(EXAMPLES "${EXAMPLES}" PARENT_SCOPE)
endfunction()

pizero_example(reloc scl::reloc)
pizero_example(static scl::static)
pizero_example(optimal scl::optimal)
pizero_example(dynamic-newlib scl::dynamic-newlib)
pizero_example(dynamic-nolib scl::dynamic-nolib)
pizero_example(xip-static scl::xip-static)
pizero_example(xip-dynamic-nolib scl::xip-dynamic-nolib)
pizero_example(xip-dynamic-newlib scl::xip-dynamic-newlib SRCS "${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared/src/image-memory.cpp")

# scl::overlay: the modules are linked with the firmware, so the linker script gains a window.
file(READ "${BOOT_LD}" OV_LD)
string(REGEX REPLACE "(PSRAM \\(rwx\\)[^\n]*\n)" "\\1    OVL   (rwx) : ORIGIN = 0x20070000, LENGTH = 16K\n" OV_LD "${OV_LD}")
string(REGEX REPLACE "(    \\.psram \\(NOLOAD\\))"
"    OVERLAY : NOCROSSREFS
    {
        .ov1 { KEEP(*(.ov1.text)) }
        .ov2 { KEEP(*(.ov2.text)) }
    } >OVL AT>FLASH
    _ov1_lma = LOADADDR(.ov1); _ov1_size = SIZEOF(.ov1);
    _ov2_lma = LOADADDR(.ov2); _ov2_size = SIZEOF(.ov2);
    _ovl_vma = ADDR(.ov1);

\\1" OV_LD "${OV_LD}")
file(WRITE "${CMAKE_BINARY_DIR}/overlay.ld" "${OV_LD}")
set_source_files_properties(overlay/ov1.cpp overlay/ov2.cpp PROPERTIES COMPILE_OPTIONS
    "-fno-pic;-mlong-calls;-fno-exceptions;-fno-rtti;-fno-unwind-tables;-fno-asynchronous-unwind-tables")
pizero_example(overlay scl::overlay LD "${CMAKE_BINARY_DIR}/overlay.ld" SRCS overlay/ov1.cpp overlay/ov2.cpp)

add_custom_target(examples DEPENDS ${EXAMPLES})
