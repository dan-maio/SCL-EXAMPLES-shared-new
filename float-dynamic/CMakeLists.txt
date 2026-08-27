# float-dynamic — POSIX dlopen of a plain -fPIC .so in RAM (Model E).
#
# Built by the six-step procedure in SCL-TESTS-shared/docs/import_in_project_full.md
# (Model E): scl::dynamic-newlib (hosted), the dl:: POSIX facade, imports resolved
# via the link-time embedded symtab (scl_embed_symtab). Configure with the HOSTED
# toolchain and -DSCL_HOSTED=ON.
cmake_minimum_required(VERSION 3.20)
project(float-dynamic LANGUAGES C CXX)

if(NOT CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=dynamic-shared/cmake/arm-none-eabi-hosted.cmake")
endif()

set(SCL_HOSTED ON CACHE BOOL "" FORCE)                 # scl::dynamic-newlib needs the hosted parts
set(SCL ${CMAKE_SOURCE_DIR}/dynamic-shared)            # platform tree; loader symlinked into ./SCL-shared
add_subdirectory(${SCL} scl)                           # -> ./SCL-shared -> scl::dynamic-newlib

# hosted WeAct platform + the POSIX dl:: facade + scl_embed_symtab()
set(SCL_PLATFORM "board" CACHE STRING "board (weact/nucleo) or qemu-f411re")
include(${SCL}/test/platforms/${SCL_PLATFORM}-hosted/cmake/platform-library.cmake)

add_executable(float-dynamic
    src/main.cpp
    ${STATIC_UNIFY_DIR}/firmware/src/dl-impl.cpp)      # the dl:: facade (dlopen/dlsym/dlclose)
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
