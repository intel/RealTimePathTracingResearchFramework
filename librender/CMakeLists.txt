# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

add_library(librender
    material.cpp
    bounds.cpp
    mesh.cpp
    scene.cpp
    lights.cpp
    quantization.cpp
    ../rendering/lights/sky_model_arhosek/sky_model.cpp
    render_backend.cpp
    gpu_programs.cpp
)
add_project_files(librender ${CMAKE_CURRENT_SOURCE_DIR} *.h *.glsl)
target_precompile_headers(librender PRIVATE ../pch.hpp)

target_include_directories(util PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>)

target_link_libraries(librender PUBLIC util)
target_link_libraries(librender PRIVATE vkr)
