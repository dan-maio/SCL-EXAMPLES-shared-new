# spifs-static — XIP+r10 float app (doc import_in_project_full §B).
# SCL imported from git; static loader (scl::xip-*); r10-based PIC library that
# executes IN PLACE from flash; by-name calls via the loader + r10 swap.
cmake_minimum_required(VERSION 3.20)
project(spifs-static LANGUAGES C CXX)
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=static-shared/cmake/arm-none-eabi.cmake")
endif()

get_filename_component(_repo ${CMAKE_SOURCE_DIR}/static-shared ABSOLUTE)   # SCL from git
add_subdirectory(${_repo} scl)
set(REF ${_repo}/test-xip/test-xip-static-direct)
set(ST_XIP ${_repo}/test-xip/test-xip-static/semihosting)
set(_test3 ${_repo}/test-flatfs)
set(_su ${_repo}/test/platforms/board-hosted/vendor)

set(SCL_PLATFORM "board" CACHE STRING "board or qemu-f411re")
if(SCL_PLATFORM STREQUAL "qemu-f411re")
    set(SCL_BOARD "qemu")
    set(XIP_BOARD_DEFS "")
else()
    set(SCL_BOARD "weact")
    set(XIP_BOARD_DEFS SCL_BOARD_WEACT=1 HSE_VALUE=25000000)
endif()
include(${_repo}/test-xip/cmake/qemu-xip.cmake)     # sets XIP_INIT/XIP_LED/XIP_FS_FILL/XIP_FLATFS_LD

set(PLATFORM_SOURCES
    ${_repo}/test-xip/common/src/semihost-file.cpp
    ${_su}/common/firmware/src/scl-log-trace.cpp
    ${XIP_LED} ${XIP_FS_FILL} ${_su}/common/firmware/src/timer.cpp ${XIP_INIT}
    ${_su}/common/firmware/src/stm32f4xx_hal_msp.c
    ${_su}/common/system/src/cmsis/system_stm32f4xx.c
    ${_su}/common/system/src/cmsis/vectors_stm32f411xe.c
    ${_su}/common/system/src/cortexm/exception-handlers.c
    ${_su}/common/system/src/cortexm/initialize-hardware.c
    ${_su}/common/system/src/cortexm/reset-hardware.c
    ${_su}/common/system/src/diag/trace.c ${_su}/common/system/src/diag/trace-impl.c
    ${_su}/common/system/src/newlib/assert.c ${_su}/common/system/src/newlib/exit.c
    ${_su}/common/system/src/newlib/sbrk.c ${_su}/common/system/src/newlib/startup.c
    ${_su}/common/system/src/newlib/syscalls.c ${_su}/common/system/src/newlib/cxx.cpp
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_cortex.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_flash.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_flash_ex.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_flash_ramfunc.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_gpio.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_iwdg.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_pwr.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_rcc.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_spi.c
    ${_su}/common/system/src/stm32f4-hal/stm32f4xx_hal_dma.c)
set_source_files_properties(${PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS "-fno-pic;-fhosted")

separate_arguments(_mcu UNIX_COMMAND "${SCL_MCU_FLAGS}")
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_mcu} -print-file-name=libsupc++.a OUTPUT_VARIABLE LIBSUPCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_mcu} -print-file-name=libgcc.a   OUTPUT_VARIABLE LIBGCC   OUTPUT_STRIP_TRAILING_WHITESPACE)

add_executable(spifs-static
    src/main.cpp src/uart.c ${PLATFORM_SOURCES}
    ${_test3}/src/fs-archive.cpp ${_test3}/src/flash-prog.c)   # loader from scl::xip-static
target_include_directories(spifs-static PRIVATE
    ${CMAKE_SOURCE_DIR}/include ${REF}/include ${ST_XIP}/include ${ST_XIP}/src
    ${_repo}/include/scl ${_repo}/include ${_test3}/include
    ${_repo}/test/platforms/board/include
    ${_su}/common/firmware/include ${_su}/common/system/include/cmsis
    ${_su}/common/system/include/stm32f4-hal ${_su}/common/system/include/cortexm
    ${_su}/common/system/include/arm ${_su}/common/system/include
    ${_repo}/test-xip/common/include)
target_compile_definitions(spifs-static PRIVATE
    DEBUG USE_FULL_ASSERT OS_USE_SEMIHOSTING TRACE OS_USE_TRACE_SEMIHOSTING_DEBUG
    STM32F411xE USE_HAL_DRIVER ${XIP_BOARD_DEFS} HARD_FLOAT OS_INCLUDE_STARTUP_GUARD_CHECKS=0
    DL_LIB_DIR="${CMAKE_SOURCE_DIR}/fixtures/build")
target_link_libraries(spifs-static PRIVATE scl::xip-static stdc++ m ${LIBSUPCXX} ${LIBGCC})
target_link_options(spifs-static PRIVATE
    -pie -nostartfiles --specs=nano.specs
    LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs
    -T${XIP_FLATFS_LD} LINKER:-Map=$<TARGET_FILE:spifs-static>.map
    # keep the HAL/libc symbols the loaded module imports in main .dynsym
    LINKER:-u,HAL_GPIO_Init LINKER:-u,HAL_GPIO_WritePin LINKER:-u,HAL_SPI_Init
    LINKER:-u,HAL_SPI_TransmitReceive LINKER:-u,HAL_SPI_Receive
    LINKER:-u,memcpy LINKER:-u,memset
    -L${CMAKE_SOURCE_DIR}/fixtures/build -l:spifs.so -lstdc++)
add_custom_command(TARGET spifs-static POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:spifs-static> $<TARGET_FILE:spifs-static>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:spifs-static> $<TARGET_FILE:spifs-static>.bin)
