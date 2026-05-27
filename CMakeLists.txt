cmake_minimum_required(VERSION 3.20)
project(roguelike-dc C)

set(CMAKE_C_STANDARD 17)

set(KOS_BASE /opt/toolchains/dc/kos)
set(KOS_CC_BASE /opt/toolchains/dc/sh-elf)

include_directories(
    src
    ${KOS_BASE}/include
    ${KOS_BASE}/kernel/arch/dreamcast/include
    ${KOS_BASE}/addons/include
)

add_definitions(
    -D__DREAMCAST__
    -D_arch_dreamcast
    -D_arch_sub_pristine
)

file(GLOB_RECURSE SOURCES src/*.c)

add_executable(roguelike ${SOURCES})
