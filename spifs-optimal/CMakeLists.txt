cmake_minimum_required(VERSION 3.20)
project(spifs-optimal C CXX)

get_filename_component(SCL "${CMAKE_CURRENT_SOURCE_DIR}/platform" ABSOLUTE)  # board support
set(SCL_HOSTED OFF)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/SCL-shared scl)   # loader submodule -> scl::optimal
set(FIX ${CMAKE_SOURCE_DIR}/fixtures)

# (loader comes from the SCL-shared submodule above: scl::optimal)

# ---- fixtures (loadable .so) ----
add_subdirectory(fixtures)

# ---- WeAct platform: reuse the vendored board init/HAL/flatfs/semihosting ----
set(SU ${SCL}/test/platforms/board-hosted/vendor)
set(T3 ${SCL}/test-flatfs)
set(PLATFORM_SOURCES
    ${SCL}/test/xip-common/src/semihost-file.cpp
    ${SU}/common/firmware/src/scl-log-trace.cpp
    ${SU}/common/firmware/src/led.cpp
    ${SU}/common/firmware/src/timer.cpp
    ${SU}/common/firmware/src/initialize-hardware.c
    ${SU}/common/firmware/src/stm32f4xx_hal_msp.c
    ${SU}/common/system/src/cmsis/system_stm32f4xx.c
    ${SU}/common/system/src/cmsis/vectors_stm32f411xe.c
    ${SU}/common/system/src/cortexm/exception-handlers.c
    ${SU}/common/system/src/cortexm/initialize-hardware.c
    ${SU}/common/system/src/cortexm/reset-hardware.c
    ${SU}/common/system/src/diag/trace.c
    ${SU}/common/system/src/diag/trace-impl.c
    ${SU}/common/system/src/newlib/assert.c
    ${SU}/common/system/src/newlib/exit.c
    ${SU}/common/system/src/newlib/sbrk.c
    ${SU}/common/system/src/newlib/startup.c
    ${SU}/common/system/src/newlib/syscalls.c
    ${SU}/common/system/src/newlib/cxx.cpp
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_cortex.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_flash.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_flash_ex.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_flash_ramfunc.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_gpio.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_iwdg.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_pwr.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_rcc.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_spi.c
    ${SU}/common/system/src/stm32f4-hal/stm32f4xx_hal_dma.c
    ${T3}/src/fs-archive.cpp
    ${T3}/src/flash-prog.c
    ${CMAKE_SOURCE_DIR}/src/uart.c
)
set_source_files_properties(${PLATFORM_SOURCES} PROPERTIES COMPILE_OPTIONS "-fno-pic;-fhosted")

separate_arguments(MCU_LIST UNIX_COMMAND "${SCL_MCU_FLAGS}")
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${MCU_LIST} -print-file-name=libsupc++.a
                OUTPUT_VARIABLE LIBSUPCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${MCU_LIST} -print-file-name=libgcc.a
                OUTPUT_VARIABLE LIBGCC OUTPUT_STRIP_TRAILING_WHITESPACE)

add_executable(spifs-optimal src/main.cpp ${PLATFORM_SOURCES})
add_dependencies(spifs-optimal fixtures)
target_include_directories(spifs-optimal PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${SCL}/test/platforms/board/include
    ${SU}/common/firmware/include
    ${SU}/common/system/include/cmsis
    ${SU}/common/system/include/stm32f4-hal
    ${SU}/common/system/include/cortexm
    ${SU}/common/system/include/arm
    ${SU}/common/system/include
    ${T3}/include
    ${SCL}/test/xip-common/include
    ${FIX}/include)
target_compile_definitions(spifs-optimal PRIVATE
    DEBUG USE_FULL_ASSERT OS_USE_SEMIHOSTING TRACE OS_USE_TRACE_SEMIHOSTING_DEBUG
    STM32F411xE USE_HAL_DRIVER SCL_BOARD_WEACT=1 HSE_VALUE=25000000 HARD_FLOAT
    OS_INCLUDE_STARTUP_GUARD_CHECKS=0
    DL_LIB_DIR="${FIX}")
target_compile_options(spifs-optimal PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include;${SCL}/test/xip-common/include/abort_fix.h>)
target_link_libraries(spifs-optimal PRIVATE scl::optimal stdc++ m ${LIBSUPCXX} ${LIBGCC})
target_link_options(spifs-optimal PRIVATE
    -pie -nostartfiles --specs=nano.specs
    LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs
    -T${CMAKE_SOURCE_DIR}/src/app-windows.ld
    LINKER:-Map=$<TARGET_FILE:spifs-optimal>.map
    # keep the HAL/libc symbols the loaded module imports in main .dynsym
    LINKER:-u,HAL_GPIO_Init LINKER:-u,HAL_GPIO_WritePin LINKER:-u,HAL_SPI_Init
    LINKER:-u,HAL_SPI_TransmitReceive LINKER:-u,HAL_SPI_Receive
    LINKER:-u,memcpy LINKER:-u,memset
    -L${FIX} -l:spifs.so -lstdc++)
add_custom_command(TARGET spifs-optimal POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:spifs-optimal> $<TARGET_FILE:spifs-optimal>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:spifs-optimal> $<TARGET_FILE:spifs-optimal>.bin)
