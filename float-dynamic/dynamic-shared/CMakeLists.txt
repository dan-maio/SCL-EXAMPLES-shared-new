cmake_minimum_required(VERSION 3.20)
# Shim: platform tree; loader from root SCL-shared (include/src/common-elf-reader symlinked).
if(NOT TARGET scl-common)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SCL-shared ${CMAKE_BINARY_DIR}/_scl-shared)
endif()
