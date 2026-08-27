# spifs-reloc — SPI-flatfs fixture over the RAM/ET_REL loader (method A).
# The fixture (W25Q64 HAL driver + flatfs + C ABI) is an ET_REL module loaded
# over semihosting; THIS firmware links the HAL so the module's HAL imports
# resolve against main's .dynsym (main linked; resolver scans it generically).
cmake_minimum_required(VERSION 3.20)
project(spifs-reloc LANGUAGES C CXX)
if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "configure with -DCMAKE_TOOLCHAIN_FILE=relocatable-shared/cmake/arm-none-eabi.cmake")
endif()

set(SCL ${CMAKE_SOURCE_DIR}/relocatable-shared)
add_subdirectory(${SCL} scl)                     # scl::reloc
set(PLAT ${SCL}/test/platforms/board)            # lean freestanding platform
set(PLAT_COMMON ${SCL}/test/platforms/common)
set(V ${CMAKE_SOURCE_DIR}/vendor)                # locally-copied STM32 HAL

add_executable(spifs-reloc
    src/main.cpp
    ${PLAT}/src/startup.c ${PLAT}/src/semihost.c ${PLAT}/src/board.c
    ${PLAT_COMMON}/src/crt.cpp ${PLAT}/src/libc-shim.c
    src/scl-link-context.cpp
    ${V}/stm32f4-hal/stm32f4xx_hal.c ${V}/stm32f4-hal/stm32f4xx_hal_cortex.c
    ${V}/stm32f4-hal/stm32f4xx_hal_gpio.c ${V}/stm32f4-hal/stm32f4xx_hal_rcc.c
    ${V}/stm32f4-hal/stm32f4xx_hal_spi.c ${V}/stm32f4-hal/stm32f4xx_hal_dma.c
    ${V}/stm32f4-hal/stm32f4xx_hal_flash.c ${V}/stm32f4-hal/stm32f4xx_hal_flash_ex.c
    ${V}/stm32f4-hal/stm32f4xx_hal_flash_ramfunc.c
    ${V}/stm32f4-hal/stm32f4xx_hal_pwr.c ${V}/stm32f4-hal/stm32f4xx_hal_pwr_ex.c
    ${V}/cmsis/system_stm32f4xx.c)

target_include_directories(spifs-reloc PRIVATE
    ${PLAT}/include ${SCL}/include
    ${V}/include ${V}/include/stm32f4-hal ${V}/include/cmsis)
target_compile_definitions(spifs-reloc PRIVATE
    SCL_BOARD_WEACT=1 HSE_VALUE=25000000 STM32F411xE USE_HAL_DRIVER
    RELOC_LIB_DIR="${CMAKE_SOURCE_DIR}/fixtures/build")
target_link_libraries(spifs-reloc PRIVATE scl::reloc)
# -fpic firmware, linked -pie: main gets a _DYNAMIC so link_context::from_self()
# can resolve the module's imports against main's own .dynsym — no resolver table.
target_compile_options(spifs-reloc PRIVATE
    -fpic -Os -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics> $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>)

# Keep the HAL/libc symbols the loaded module imports (main never calls them) in
# main's .dynsym: -u anchors force them into the link, -Wl,-E exports them.
set(_uflags "")
foreach(a HAL_GPIO_Init HAL_GPIO_WritePin HAL_SPI_Init HAL_SPI_TransmitReceive HAL_SPI_Receive memcpy memset)
    list(APPEND _uflags "LINKER:-u,${a}")
endforeach()

separate_arguments(_mcu UNIX_COMMAND "${SCL_MCU_FLAGS}")
execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_mcu} -print-file-name=libsupc++.a OUTPUT_VARIABLE _libsupcxx OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_C_COMPILER}   ${_mcu} -print-file-name=libgcc.a   OUTPUT_VARIABLE _libgcc   OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_C_COMPILER}   ${_mcu} -print-file-name=libm.a     OUTPUT_VARIABLE _libm     OUTPUT_STRIP_TRAILING_WHITESPACE)
target_link_options(spifs-reloc PRIVATE
    -pie -nostartfiles --specs=nano.specs
    LINKER:-Bsymbolic LINKER:-E LINKER:--unresolved-symbols=ignore-in-shared-libs
    -L${CMAKE_SOURCE_DIR}/ldscripts -L${SCL}/test/platforms/board/ldscripts
    -T${SCL}/test/platforms/board/ldscripts/sections.ld

    ${_uflags}
    -Wl,--gc-sections -Wl,--no-warn-rwx-segments
    -Wl,-Map=$<TARGET_FILE:spifs-reloc>.map)
target_link_libraries(spifs-reloc PRIVATE ${_libsupcxx} ${_libgcc} ${_libm})
add_custom_command(TARGET spifs-reloc POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:spifs-reloc> $<TARGET_FILE:spifs-reloc>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:spifs-reloc> $<TARGET_FILE:spifs-reloc>.bin)
