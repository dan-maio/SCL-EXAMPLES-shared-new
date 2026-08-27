# float-dynamic — POSIX dlopen of a plain -fPIC .so in RAM (Model E).
#
# Structure of every example here:
#   SCL-shared/   the loader — a git submodule (the ONLY dependency; cloned
#                 recursively). Provides scl::* as INTERFACE source libraries.
#   platform/     the WeAct F411CE board support we must supply because this is a
#                 bare example (startup, HAL, semihosting, the dl:: facade). A
#                 real application already has its own — drop platform/ and point
#                 the include() at your project's board files instead.
#   fixtures/     the loadable module(s); src/ + this file are the app itself.
#
# Model E steps (SCL-shared/docs or SCL-TESTS-shared/docs/import_in_project_full.md):
#   scl::dynamic-newlib (hosted) + the dl:: POSIX facade; imports resolved via the
#   link-time embedded symtab (scl_embed_symtab). Hosted toolchain, SCL_HOSTED=ON.
cmake_minimum_required(VERSION 3.20)
project(float-dynamic LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=platform/cmake/arm-none-eabi-hosted.cmake")
endif()

# ---- the loader (the one git dependency) ------------------------------------
set(SCL_HOSTED ON CACHE BOOL "" FORCE)                 # scl::dynamic-newlib needs the hosted parts
add_subdirectory(${CMAKE_SOURCE_DIR}/SCL-shared scl)   # -> scl::dynamic-newlib (+ gen-symtab-host)

# ---- the board support (we ship it; a real app already has its own) ---------
set(SCL_PLATFORM "board" CACHE STRING "board (weact/nucleo) or qemu-f411re")
include(${CMAKE_SOURCE_DIR}/platform/test/platforms/${SCL_PLATFORM}-hosted/cmake/platform-library.cmake)

add_executable(float-dynamic
    src/main.cpp
    src/uart.c                                         # USART1 PA9 115200 (bare-metal)
    ${STATIC_UNIFY_DIR}/firmware/src/dl-impl.cpp)      # the dl:: facade (dlopen/dlsym/dlclose)
target_include_directories(float-dynamic PRIVATE src)
target_link_libraries(float-dynamic PRIVATE
    platform::${SCL_PLATFORM}-hosted
    scl::dynamic-newlib)
target_compile_definitions(float-dynamic PRIVATE
    SCL_SO_DIR="${CMAKE_SOURCE_DIR}/fixtures/build")

# Keep-alive anchors: floatlib borrows the float helpers from main; without -u
# they would be gc'd before reaching main's embedded symtab.
foreach(_a printf puts malloc free __aeabi_fadd __aeabi_fmul __aeabi_ui2f __aeabi_f2uiz)
    list(APPEND _uflags "LINKER:-u,${_a}")
endforeach()
target_link_options(float-dynamic PRIVATE ${_uflags}
    LINKER:-Map=$<TARGET_FILE:float-dynamic>.map)

scl_embed_symtab(float-dynamic)                        # bake main's symbols into flash

add_custom_command(TARGET float-dynamic POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:float-dynamic> $<TARGET_FILE:float-dynamic>.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:float-dynamic> $<TARGET_FILE:float-dynamic>.bin)
