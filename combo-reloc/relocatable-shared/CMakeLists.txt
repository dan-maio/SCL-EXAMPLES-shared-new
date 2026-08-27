cmake_minimum_required(VERSION 3.20)
# Shim: this dir is the example platform/test tree; the LOADER comes from the
# shared SCL-shared clone at the examples root (include/ src/ common-elf-reader/
# are symlinks into it). add_subdirectory(${SCL}) lands here and forwards.
if(NOT TARGET scl-common)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SCL-shared ${CMAKE_BINARY_DIR}/_scl-shared)
endif()
