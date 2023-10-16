# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

option(ENABLE_RENDERING_TESTS "Build additional rendering engineering tests" OFF)
option(ENABLE_RENDERING_TOOLS "Build additional rendering dev tools" OFF)

add_custom_target(rendering)
add_project_files(rendering ${CMAKE_CURRENT_SOURCE_DIR} SOURCE_GROUP Shaders *.glsl *.h *.hpp)

add_library(test_shaders STATIC tests/compile.cpp)
target_link_libraries(test_shaders PUBLIC glm)

if (ENABLE_RENDERING_TESTS)
  add_executable(test_gltf tests/gltf_bsdf.cpp)
endif ()

if (ENABLE_RENDERING_TOOLS)
  add_executable(prepare_sobol tools/prepare_sobol.cpp)
endif ()

# IDE filters
set_main_targets(rendering)
