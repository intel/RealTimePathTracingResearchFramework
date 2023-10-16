# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

add_library(libapp
    benchmark_info.cpp
    shell.cpp
    scene_state.cpp
    app_state.cpp
    imutils.cpp
)
add_project_files(libapp ${CMAKE_CURRENT_SOURCE_DIR} *.h)
target_precompile_headers(libapp REUSE_FROM util)

target_include_directories(libapp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>)
target_link_libraries(libapp PUBLIC
    librender
    util
    display)
