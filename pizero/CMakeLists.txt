# pizero — the WeAct float, spifs and combo examples, for every SCL loader, on the Pi-Zero
# RP2350B under the µOS++ III SMP kernel: loaded at boot on core 0, then used from both cores.
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

# ---- the WeAct examples' own fixtures, in each form a loader takes -------------------
# Only w25q64.cpp differs: the Pi-Zero has neither a W25Q64 nor the STM32 HAL.
set(WEACT "${CMAKE_CURRENT_SOURCE_DIR}/../combo-reloc/fixtures")
set(FLOAT_SRCS "${WEACT}/src/floatlib.cpp")
set(SPIFS_SRCS "${WEACT}/src/spi-shim.cpp" "${WEACT}/src/fs-archive-spi.cpp" "${WEACT}/src/spifs-api.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/w25q64-rp2350.cpp")
set(LDS "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts")
set(FIX "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
file(MAKE_DIRECTORY "${FIX}")
set(MCU -mcpu=cortex-m33 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16)
set(LIB_CXX -Os -std=c++20 -ffreestanding -fno-common -fno-exceptions -fno-rtti -fno-threadsafe-statics
    -fno-use-cxa-atexit -I${WEACT}/include -I${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared/common-elf-reader/include)
set(SO_LINK -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E)
set(FIX_FILES "")
function(fixture name slot)
    set(srcs ${ARGN})
    # scl::reloc: an ET_REL object.
    add_custom_command(OUTPUT "${FIX}/${name}.rel.o"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -mlong-calls -fno-pic -r -nostdlib ${srcs} -o "${FIX}/${name}.rel.o"
        DEPENDS ${srcs} VERBATIM)
    # scl::dynamic-nolib and the XIP loaders: r10 single-pic-base, with text and data split.
    add_custom_command(OUTPUT "${FIX}/${name}.so"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -mlong-calls -fpic -msingle-pic-base -mpic-register=r10
                -mno-pic-data-is-text-relative ${SO_LINK} -Wl,-Bsymbolic -T "${LDS}/xip-split.ld" ${srcs} -o "${FIX}/${name}.so"
        DEPENDS ${srcs} "${LDS}/xip-split.ld" VERBATIM)
    # scl::static and scl::dynamic-newlib: a plain -fPIC .so.
    add_custom_command(OUTPUT "${FIX}/${name}.pic.so"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -fPIC ${SO_LINK} ${srcs} -o "${FIX}/${name}.pic.so"
        DEPENDS ${srcs} VERBATIM)
    # scl::optimal: -fno-pic, linked at this slot of the fixed windows in common/arena.S.
    math(EXPR tb "0x20070000 + ${slot} * 0x1000" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR db "0x20078000 + ${slot} * 0x1000" OUTPUT_FORMAT HEXADECIMAL)
    add_custom_command(OUTPUT "${FIX}/${name}.opt.so"
        COMMAND sed -e "s/__TEXT_BASE__/${tb}/g" -e "s/__DATA_BASE__/${db}/g" "${LDS}/nopic-split.ld" > "${FIX}/${name}.opt.ld"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} ${LIB_CXX} -fno-pic -mlong-calls ${SO_LINK} -Wl,-z,max-page-size=0x100
                -Wl,--unresolved-symbols=ignore-all -T "${FIX}/${name}.opt.ld" ${srcs} -o "${FIX}/${name}.opt.so"
        DEPENDS ${srcs} "${LDS}/nopic-split.ld")
    set(FIX_FILES ${FIX_FILES} "${FIX}/${name}.rel.o" "${FIX}/${name}.so" "${FIX}/${name}.pic.so" "${FIX}/${name}.opt.so" PARENT_SCOPE)
endfunction()
fixture(floatlib 0 ${FLOAT_SRCS})
fixture(spifs 1 ${SPIFS_SRCS})
add_custom_target(fixtures DEPENDS ${FIX_FILES})

# ---- scl::overlay: the same fixtures, linked with the firmware, in one SRAM window -------
set(OV_FLOAT "${CMAKE_CURRENT_SOURCE_DIR}/../combo-overlay/fixtures/src/ovfloat.cpp")
set_source_files_properties(${OV_FLOAT} ${SPIFS_SRCS} PROPERTIES COMPILE_OPTIONS
    "-fno-pic;-mlong-calls;-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit;-fno-threadsafe-statics;-fno-unwind-tables;-fno-asynchronous-unwind-tables")
set(OV_PICK ".text .text.* .rodata .rodata.* .data .data.*")
file(READ "${BOOT_LD}" OV_LD)
string(REGEX REPLACE "(PSRAM \\(rwx\\)[^\n]*\n)" "\\1    OVL   (rwx) : ORIGIN = 0x20070000, LENGTH = 16K\n" OV_LD "${OV_LD}")
# After .text, so the boot header stays first in flash; .text leaves the libraries' objects.
set(OV_OBJS "*ovfloat.cpp.obj *spi-shim.cpp.obj *fs-archive-spi.cpp.obj *spifs-api.cpp.obj *w25q64-rp2350.cpp.obj")
string(REPLACE "*(.text*)" "*(EXCLUDE_FILE(${OV_OBJS}) .text*)" OV_LD "${OV_LD}")
string(REPLACE "*(.rodata*)" "*(EXCLUDE_FILE(${OV_OBJS}) .rodata*)" OV_LD "${OV_LD}")
string(REGEX REPLACE "(\n[ \t]*\\.preinit_array[ \t]*:)"
"
    /* A constructor in the window can't run at startup, when the window is empty. The only
       one is the shim's empty w25q64(), so drop it. */
    /DISCARD/ : { *spi-shim.cpp.obj(.init_array*) }

    OVERLAY : NOCROSSREFS
    {
        .ovfloat { KEEP(*ovfloat.cpp.obj(${OV_PICK})) }
        .ovspifs {
            KEEP(*spi-shim.cpp.obj(${OV_PICK}))
            KEEP(*fs-archive-spi.cpp.obj(${OV_PICK}))
            KEEP(*spifs-api.cpp.obj(${OV_PICK}))
            KEEP(*w25q64-rp2350.cpp.obj(${OV_PICK}))
        }
    } >OVL AT>FLASH
    _ovfloat_lma = LOADADDR(.ovfloat); _ovfloat_size = SIZEOF(.ovfloat);
    _ovspifs_lma = LOADADDR(.ovspifs); _ovspifs_size = SIZEOF(.ovspifs);
    _ovl_vma = ADDR(.ovfloat);
\\1" OV_LD "${OV_LD}")
file(WRITE "${CMAKE_BINARY_DIR}/overlay.ld" "${OV_LD}")

# ---- one firmware per fixture and loader ------------------------------------------------
function(pizero_example app loader target)
    set(name "${app}-${loader}")
    set(srcs apps/${app}.cpp loaders/${loader}.cpp common/pizero.cpp common/arena.S ${BOARD_SRCS})
    set(ld "${BOOT_LD}")
    if(loader STREQUAL "overlay")
        set(ld "${CMAKE_BINARY_DIR}/overlay.ld")
        list(APPEND srcs ${OV_FLOAT} ${SPIFS_SRCS})
    endif()
    if(loader STREQUAL "xip-dynamic-newlib")
        list(APPEND srcs "${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared/src/image-memory.cpp")
    endif()
    uos_add_app("${name}-hwd"
        SOURCES ${srcs}
        NCPU 2
        LINKER_SCRIPT "${ld}"
        DEFINES TRACE SEMIHOST HW_BUILD EXAMPLE_FIXTURE_DIR="${FIX}" LOADER_NAME="${loader}"
        INCLUDES "${UOS_PORT_DIR}/test/boards/shared" "${CMAKE_CURRENT_SOURCE_DIR}/common"
                 "${CMAKE_CURRENT_SOURCE_DIR}/apps" "${WEACT}/include"
                 "${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared/common-elf-reader/include"
        LIBRARIES micro-os-plus::cortexm ${target})
    add_dependencies("${name}-hwd" fixtures)
    target_link_options("${name}-hwd" PRIVATE -Wl,--wrap=main)   # load after constructors, before the kernel
    set_target_properties("${name}-hwd" PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
    list(APPEND EXAMPLES "${name}-hwd")
    set(EXAMPLES "${EXAMPLES}" PARENT_SCOPE)
endfunction()

foreach(app float spifs combo)
    pizero_example(${app} reloc scl::reloc)
    pizero_example(${app} static scl::static)
    pizero_example(${app} optimal scl::optimal)
    pizero_example(${app} overlay scl::overlay)
    pizero_example(${app} dynamic-newlib scl::dynamic-newlib)
    pizero_example(${app} dynamic-nolib scl::dynamic-nolib)
    pizero_example(${app} xip-static scl::xip-static)
    pizero_example(${app} xip-dynamic-nolib scl::xip-dynamic-nolib)
    pizero_example(${app} xip-dynamic-newlib scl::xip-dynamic-newlib)
endforeach()

add_custom_target(examples DEPENDS ${EXAMPLES})
