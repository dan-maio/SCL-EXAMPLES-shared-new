# luckfox-lyra — the WeAct float, SD-card, combo, cpp, provcons, slib, drivers and
# exception examples for every SCL loader, on the Luckfox Lyra B (RK3506, 3x Cortex-A7)
# under the µOS++ III SMP kernel: loaded at boot on core 0, then used from all three cores.
#
# The board has NO execute-in-place application flash. "XIP" here means the image is
# patched into a static DRAM region and run in place from there. Storage is the microSD
# card (the board's boot card is mounted, never formatted).
#
# Board support comes from the aarch32 port repository. Run with ./run-all-lyra.sh.
cmake_minimum_required(VERSION 3.20)

set(UOS_PORT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../uos/micro-os-plus-iii-aarch32"
    CACHE PATH "micro-os-plus-iii-aarch32 working copy")
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${UOS_PORT_DIR}/../micro-os-plus-iii/cmake/toolchains/arm-none-eabi.cmake")
endif()

project(scl-luckfox-lyra-examples LANGUAGES C CXX ASM)

set(BOARD "luckfox-lyra" CACHE STRING "" FORCE)
add_subdirectory("${UOS_PORT_DIR}" uos)

set(SCL_HOSTED ON CACHE BOOL "" FORCE)     # scl::dynamic-newlib and its XIP form need it
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared" scl)

set(BOARD_SUPPORT "${UOS_PORT_DIR}/test/luckfox-lyra/src/test-smp-boot.cpp")
set(BOARD_INC "${UOS_PORT_DIR}/test/luckfox-lyra/include")
set(BOARD_LD "${UOS_PORT_DIR}/test/boards/luckfox-lyra/linker.ld")

# ---- the fixtures, in each form a loader takes -----------------------------------------
# Fixtures build as Thumb, as on the Cortex-M boards: the loaders' ET_REL relocations are
# Thumb, while main is ARM (the board's -marm).
set(MCU -mcpu=cortex-a7 -mthumb -mfloat-abi=hard -mfpu=neon-vfpv4)
set(FIX_CXX -std=c++20 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit)
set(T "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tests")
set(FIX_INC -I${T}/include -I${T}/c1 -I${T}/c2 -I${T}/cpp3 -I${T}/mylib1
            -I${T}/mylib2 -I${T}/libprov -I${T}/libcons)
set(FIX "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
file(MAKE_DIRECTORY "${FIX}")
set(LD_SPLIT "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/xip-split.ld")
set(LD_NOPIC "${CMAKE_CURRENT_SOURCE_DIR}/ldscripts/nopic-split.ld")
set(WIN_TEXT 0x07A00000)
set(WIN_DATA 0x07A80000)
set(FIX_FILES "")
# name slot opts sources: the four forms the loaders take.
function(fixture name slot opts)
    set(srcs ${ARGN})
    add_custom_command(OUTPUT "${FIX}/${name}.rel.o"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} -mlong-calls -mword-relocations -fno-pic -Os -r -nostdlib ${${opts}} ${srcs} -o "${FIX}/${name}.rel.o"
        DEPENDS ${srcs} VERBATIM)
    add_custom_command(OUTPUT "${FIX}/${name}.so"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} -fpic -msingle-pic-base -mpic-register=r10 -mno-pic-data-is-text-relative -O0
                -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E -Wl,-Bsymbolic -T "${LD_SPLIT}" ${${opts}} ${srcs} -o "${FIX}/${name}.so"
        DEPENDS ${srcs} "${LD_SPLIT}" VERBATIM)
    add_custom_command(OUTPUT "${FIX}/${name}.pic.so"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} -fPIC -O0 -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E ${${opts}} ${srcs} -o "${FIX}/${name}.pic.so"
        DEPENDS ${srcs} VERBATIM)
    math(EXPR tb "${WIN_TEXT} + ${slot} * 0x1000" OUTPUT_FORMAT HEXADECIMAL)
    math(EXPR db "${WIN_DATA} + ${slot} * 0x1000" OUTPUT_FORMAT HEXADECIMAL)
    add_custom_command(OUTPUT "${FIX}/${name}.opt.so"
        COMMAND sed -e "s/__TEXT_BASE__/${tb}/g" -e "s/__DATA_BASE__/${db}/g" "${LD_NOPIC}" > "${FIX}/${name}.opt.ld"
        COMMAND ${CMAKE_CXX_COMPILER} ${MCU} -fno-pic -mword-relocations -mlong-calls -O0 -shared -nostartfiles -nostdlib -nodefaultlibs -Wl,-E
                -Wl,-z,max-page-size=0x100 -Wl,--unresolved-symbols=ignore-all -T "${FIX}/${name}.opt.ld" ${${opts}} ${srcs} -o "${FIX}/${name}.opt.so"
        DEPENDS ${srcs} "${LD_NOPIC}")
    set(FIX_FILES ${FIX_FILES} "${FIX}/${name}.rel.o" "${FIX}/${name}.so" "${FIX}/${name}.pic.so" "${FIX}/${name}.opt.so" PARENT_SCOPE)
endfunction()

set(KIND_C -std=gnu11 -ffreestanding -fno-common ${FIX_INC} -x c)
set(KIND_CPP ${FIX_CXX} -ffreestanding -fno-common ${FIX_INC} -x c++)
set(KIND_EH -std=c++20 -fno-threadsafe-statics -I${T}/libcppeh/include -I${T}/libcppsh/include -x c++)
set(KIND_RK -std=gnu11 -ffreestanding -fno-common -x c)
fixture(cpp3 0 KIND_CPP "${T}/cpp3/cpp3.cpp")
fixture(c2 1 KIND_C "${T}/c2/c2.c")
fixture(c1 2 KIND_C "${T}/c1/c1.c")
fixture(mylib1 3 KIND_C "${T}/mylib1/mylib1.c")
fixture(mylib2 4 KIND_C "${T}/mylib2/mylib2.c")
fixture(libprov 5 KIND_C "${T}/libprov/libprov.c")
fixture(libcons 6 KIND_C "${T}/libcons/libcons.c")
fixture(slib 7 KIND_C "${T}/slib/slib.c")
fixture(libcppeh 0 KIND_EH "${T}/libcppeh/src/cppeh.cpp")
fixture(ehx_catch 1 KIND_EH "${T}/ehx/ehx-catch.cpp")
fixture(libcppsh 2 KIND_EH "${T}/libcppsh/src/cppsh-api.cpp" "${T}/libcppsh/src/widget.cpp")
fixture(libled 0 KIND_RK "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rk3506/led.c")
fixture(libuart 1 KIND_RK "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rk3506/uart.c")
fixture(libsd 2 KIND_RK "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rk3506/sd.c")
fixture(floatlib 0 KIND_CPP "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/floatlib.cpp")
fixture(sd 0 KIND_RK "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rk3506/sd.c")
add_custom_target(fixtures DEPENDS ${FIX_FILES})

# ---- scl::overlay: the same libraries, linked with the firmware, in one DRAM window ------
# The float library and the SD library time-share the window. They are linked at the
# window's VMA but their load image is kept in RAM (AT>RAM), so swapping is a copy.
set(OV_FLOAT "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ovfloat.cpp")
set(OV_SD "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/rk3506/sd.c")
set_source_files_properties(${OV_FLOAT} ${OV_SD} PROPERTIES COMPILE_OPTIONS
    "-fno-pic;-mlong-calls;-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit;-fno-threadsafe-statics;-fno-unwind-tables;-fno-asynchronous-unwind-tables")
set(OV_PICK ".text .text.* .rodata .rodata.* .data .data.*")
file(READ "${BOARD_LD}" OV_LD)
string(REGEX REPLACE "(RAM \\(rwx\\)[^\n]*\n)" "\\1   OVL (rwx) : ORIGIN = 0x07A00000, LENGTH = 16K\n" OV_LD "${OV_LD}")
# .text, .rodata and .data leave the two overlay objects; their .bss stays in the firmware.
set(OV_OBJS "*ovfloat.cpp.obj *sd.c.obj")
string(REPLACE "*(.text*)" "*(EXCLUDE_FILE(${OV_OBJS}) .text*)" OV_LD "${OV_LD}")
string(REPLACE "*(.rodata*)" "*(EXCLUDE_FILE(${OV_OBJS}) .rodata*)" OV_LD "${OV_LD}")
string(REPLACE "*(.data*)" "*(EXCLUDE_FILE(${OV_OBJS}) .data*)" OV_LD "${OV_LD}")
string(REGEX REPLACE "(    \\.bss \\(NOLOAD\\))"
"    OVERLAY : NOCROSSREFS
    {
        .ovfloat { KEEP(*ovfloat.cpp.obj(${OV_PICK})) }
        .ovsd    { KEEP(*sd.c.obj(${OV_PICK})) }
    } >OVL AT>RAM
    _ovfloat_lma = LOADADDR(.ovfloat); _ovfloat_size = SIZEOF(.ovfloat);
    _ovsd_lma = LOADADDR(.ovsd); _ovsd_size = SIZEOF(.ovsd);
    _ovl_vma = ADDR(.ovfloat);

\\1" OV_LD "${OV_LD}")
file(WRITE "${CMAKE_BINARY_DIR}/overlay.ld" "${OV_LD}")

# The WeAct overlay test's fixtures, which mark their own sections.
set(OVFIX_SRCS "${T}/overlay/ov1.c" "${T}/overlay/ov2.c" "${T}/overlay/ovcpp.cpp")
set_source_files_properties("${T}/overlay/ov1.c" "${T}/overlay/ov2.c" PROPERTIES COMPILE_OPTIONS "-fno-pic;-mlong-calls")
set_source_files_properties("${T}/overlay/ovcpp.cpp" PROPERTIES COMPILE_OPTIONS
    "-fno-pic;-mlong-calls;-fno-exceptions;-fno-rtti;-fno-use-cxa-atexit;-fno-unwind-tables;-fno-asynchronous-unwind-tables")
file(READ "${BOARD_LD}" OVFIX_LD)
string(REGEX REPLACE "(RAM \\(rwx\\)[^\n]*\n)" "\\1   OVL (rwx) : ORIGIN = 0x07A00000, LENGTH = 16K\n" OVFIX_LD "${OVFIX_LD}")
string(REGEX REPLACE "(    \\.bss \\(NOLOAD\\))"
"    OVERLAY : NOCROSSREFS
    {
        .ov1   { KEEP(*(.ov1.text))   KEEP(*(.ov1.rodata))   KEEP(*(.ov1.data)) }
        .ov2   { KEEP(*(.ov2.text))   KEEP(*(.ov2.rodata))   KEEP(*(.ov2.data)) }
        .ovcpp { KEEP(*(.ovcpp.text)) KEEP(*(.ovcpp.rodata)) KEEP(*(.ovcpp.data)) }
    } >OVL AT>RAM
    _ov1_lma = LOADADDR(.ov1);     _ov1_size = SIZEOF(.ov1);
    _ov2_lma = LOADADDR(.ov2);     _ov2_size = SIZEOF(.ov2);
    _ovcpp_lma = LOADADDR(.ovcpp); _ovcpp_size = SIZEOF(.ovcpp);
    _ovl_vma = ADDR(.ov1);

\\1" OVFIX_LD "${OVFIX_LD}")
file(WRITE "${CMAKE_BINARY_DIR}/ovfix.ld" "${OVFIX_LD}")

# Exceptions: nano.specs' __cxa_throw has no unwind tables; the full libsupc++ does.
execute_process(COMMAND ${CMAKE_CXX_COMPILER} -mcpu=cortex-a7 -marm -mfloat-abi=hard -mfpu=neon-vfpv4 -print-file-name=libsupc++.a
                OUTPUT_VARIABLE LIBSUPCXX OUTPUT_STRIP_TRAILING_WHITESPACE)

# ---- one firmware per app and loader ----------------------------------------------------
set(SD_APPS sd combo drivers)
function(lyra_example app loader target)
    set(name "${app}-${loader}")
    set(srcs apps/${app}.cpp common/lyra.cpp common/host-imports.cpp common/arena.S ${BOARD_SUPPORT})
    set(ld "${BOARD_LD}")
    set(defs "")
    set(libs micro-os-plus::aarch32 ${target})
    if(app STREQUAL "ovfix")
        set(ld "${CMAKE_BINARY_DIR}/ovfix.ld")
        list(APPEND srcs ${OVFIX_SRCS})
        set(libs micro-os-plus::aarch32 scl::overlay)
    else()
        list(APPEND srcs loaders/${loader}.cpp)
    endif()
    if(loader STREQUAL "overlay" AND NOT app STREQUAL "ovfix")
        set(ld "${CMAKE_BINARY_DIR}/overlay.ld")
        list(APPEND srcs ${OV_FLOAT} ${OV_SD})
    endif()
    if(app IN_LIST SD_APPS)
        list(APPEND srcs common/board-sd.cpp)
        list(APPEND libs micro-os-plus::devices-rk3506)
    endif()
    if(app STREQUAL "cppeh")
        list(APPEND defs APP_EH)
    endif()
    if(loader STREQUAL "xip-dynamic-newlib")
        list(APPEND srcs "${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared/src/image-memory.cpp")
    endif()
    uos_add_app("${name}-hwd"
        SOURCES ${srcs}
        NCPU 3
        LINKER_SCRIPT "${ld}"
        DEFINES TRACE SEMIHOST HW_BUILD EXAMPLE_FIXTURE_DIR="${FIX}" LOADER_NAME="${loader}" ${defs}
        INCLUDES "${BOARD_INC}" "${CMAKE_CURRENT_SOURCE_DIR}/common" "${CMAKE_CURRENT_SOURCE_DIR}/apps"
                 "${CMAKE_CURRENT_SOURCE_DIR}/fixtures" "${T}/include" "${T}/libcppeh/include"
                 "${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared/common-elf-reader/include"
        LIBRARIES ${libs})
    add_dependencies("${name}-hwd" fixtures)
    target_compile_definitions("${name}-hwd" PRIVATE $<$<COMPILE_LANGUAGE:ASM>:SCL_ARENA_BYTES=98304>)
    if(app STREQUAL "cppeh")
        target_link_libraries("${name}-hwd" PRIVATE "${LIBSUPCXX}")
        target_link_options("${name}-hwd" PRIVATE -Wl,--wrap=malloc -Wl,--wrap=free)
        target_compile_options("${name}-hwd" PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fexceptions> $<$<COMPILE_LANGUAGE:CXX>:-frtti>)
    endif()
    set_target_properties("${name}-hwd" PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")
    list(APPEND EXAMPLES "${name}-hwd")
    set(EXAMPLES "${EXAMPLES}" PARENT_SCOPE)
endfunction()

set(ALL_LOADERS reloc static optimal dynamic-newlib dynamic-nolib xip-static xip-dynamic-nolib xip-dynamic-newlib)
function(lyra_app app)
    foreach(loader ${ARGN})
        lyra_example(${app} ${loader} scl::${loader})
    endforeach()
    set(EXAMPLES "${EXAMPLES}" PARENT_SCOPE)
endfunction()
lyra_app(float ${ALL_LOADERS} overlay)
lyra_app(sd ${ALL_LOADERS} overlay)
lyra_app(combo ${ALL_LOADERS} overlay)
lyra_app(cpp reloc optimal dynamic-newlib dynamic-nolib xip-static xip-dynamic-nolib xip-dynamic-newlib)   # static: no cyclic imports
lyra_app(provcons ${ALL_LOADERS})
lyra_app(slib ${ALL_LOADERS})
lyra_app(drivers ${ALL_LOADERS})
lyra_app(cppeh ${ALL_LOADERS})
lyra_app(ovfix overlay)

add_custom_target(examples DEPENDS ${EXAMPLES})
