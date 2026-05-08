cmake_minimum_required(VERSION 3.13)
project(cyanfs LANGUAGES C)
add_compile_options(-Wall -Werror -O2)
add_link_options(-s)
file (GLOB_RECURSE core_src CONFIGURE_DEPENDS "../core/*.c")

if (NOT "${CYANFS_DEBUG}" STREQUAL "")
add_compile_definitions(CYANFS_DEBUG_ENABLED)
endif()

add_executable(cyanfs.blkid blkid.c utils.c ${core_src})
target_compile_definitions(cyanfs.blkid PUBLIC CYANFS_GLIBC)

add_executable(cyanfs.mkfs mkfs.c utils.c ${core_src})
target_compile_definitions(cyanfs.mkfs PUBLIC CYANFS_GLIBC)

add_executable(cyanfs.edit edit.c utils.c ${core_src})
target_compile_definitions(cyanfs.edit PUBLIC CYANFS_PTHREAD)
target_link_libraries(cyanfs.edit PUBLIC pthread)
