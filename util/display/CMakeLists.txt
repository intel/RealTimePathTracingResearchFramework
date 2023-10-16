# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

find_package(OpenGL REQUIRED)

add_library(display
    imgui_backend.cpp
    gldisplay.cpp
    shader.cpp)
target_link_libraries(display PUBLIC util glad)

target_link_libraries(display PUBLIC imgui)
target_link_libraries(display PUBLIC glfw)
