# combo-dynamic — POSIX dlopen of a plain -fPIC SPI-flatfs .so in RAM (Model E).
#
#   SCL-shared/  the loader (git submodule; the only dependency).
#   platform/    the WeAct board support we ship (a real app has its own).
# Model E: scl::dynamic-newlib (hosted) + the dl:: POSIX facade; imports (the
# STM32 HAL + memcpy/memset the module borrows) resolved via scl_embed_symtab.
cmake_minimum_required(VERSION 3.20)
project(combo-dynamic LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=platform/cmake/arm-none-eabi-hosted.cmake")
endif()

set(SCL_HOSTED ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/SCL-shared scl)

set(SCL_PLATFORM "board" CACHE STRING "board (weact/nucleo) or qemu-f411re")
include(${CMAKE_SOURCE_DIR}/platform/test/platforms/${SCL_PLATFORM}-hosted/cmake/platform-library.cmake)

add_executable(combo-dynamic
    src/main.cpp
    src/uart.c
    ${STATIC_UNIFY_DIR}/firmware/src/dl-impl.cpp
    # the board-hosted platform ships GPIO/RCC/FLASH HAL but not SPI; spifs.so
    # imports HAL_SPI_* from main, so compile the SPI HAL module in too (+ DMA,
    # which hal_spi.c's DMA paths reference).
    ${STATIC_UNIFY_DIR}/common/system/src/stm32f4-hal/stm32f4xx_hal_spi.c
    ${STATIC_UNIFY_DIR}/common/system/src/stm32f4-hal/stm32f4xx_hal_dma.c)
target_include_directories(combo-dynamic PRIVATE src)
target_link_libraries(combo-dynamic PRIVATE
    platform::${SCL_PLATFORM}-hosted
    scl::dynamic-newlib)
target_compile_definitions(combo-dynamic PRIVATE
    SCL_SO_DIR="${CMAKE_SOURCE_DIR}/fixtures/build")

# Keep-alive anchors: spifs.so imports the STM32 HAL + memcpy/memset from main.
foreach(_a printf puts snprintf malloc free memcpy memset
    HAL_GPIO_Init HAL_GPIO_WritePin HAL_SPI_Init HAL_SPI_Receive HAL_SPI_TransmitReceive)
    list(APPEND _uflags "LINKER:-u,${_a}")
endforeach()
target_link_options(combo-dynamic PRIVATE ${_uflags}
    LINKER:-Map=$<TARGET_FILE:combo-dynamic>.map)

scl_embed_symtab(combo-dynamic)

add_custom_command(TARGET combo-dynamic POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:combo-dynamic> $<TARGET_FILE:combo-dynamic>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:combo-dynamic> $<TARGET_FILE:combo-dynamic>.bin)
