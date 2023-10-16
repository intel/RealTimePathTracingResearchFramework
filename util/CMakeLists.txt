# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

begin_support_targets() # IDE filters
add_subdirectory(display)



add_library(util
    interactive_camera.cpp
    util.cpp
    profiling.cpp
    error_io.cpp
    file_mapping.cpp
    device_backend.cpp
    write_image.cpp
    image.cpp
    lod.cpp
    sha1_bytes.cpp

    )
add_project_files(util ${CMAKE_CURRENT_SOURCE_DIR} *.h)

target_precompile_headers(util PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/../pch.hpp>)
target_precompile_headers(display REUSE_FROM util)

# make these modules known to all dependent targets
target_include_directories(util PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    ${GLFW_INTERFACE_DEFINITIONS})
target_compile_definitions(util PUBLIC ${GLM_INTERFACE_DEFINITIONS})

target_link_libraries(util PUBLIC glm)
target_link_libraries(util PUBLIC imgui)
target_link_libraries(util PUBLIC tinyexr)
target_link_libraries(util PUBLIC stb)
target_link_libraries(util PRIVATE crypto-algorithms)
#target_link_libraries(util PRIVATE parallel_hashmap json tiny_gltf)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_executable(compare_exr compare_exr.cpp)
target_link_libraries(compare_exr PRIVATE util tinyexr)

# IDE filters
end_support_targets()
set_main_targets(util)
