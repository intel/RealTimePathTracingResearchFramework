# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16)

# ------------------------------------------------------------------------------
# Available configurations
# ------------------------------------------------------------------------------

include(CMakeDependentOption)

option(ENABLE_VULKAN "Build the Vulkan rendering backend. Requires Vulkan." ON)

if (NOT WIN32 OR CMAKE_BUILD_TYPE MATCHES "Release|RELEASE|RelWithDebInfo|RELWITHDEBINFO")
    option(ENABLE_PYTHON "Enable modules for Python integration" ON)
else ()
    # todo: this should not be necessary, but cannot expect python dev environment by default?
    option(ENABLE_PYTHON "Enable modules for Python integration" OFF)
endif ()

option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." ${UNIX})

# todo: some of these have become optimizations decoupled from target HW, separate these
option(COMPILING_FOR_DG2 "Enable DG2 extensions and optimizations" ON)

# Optional language integrations (potentially requires additional SDKs, build tools, and higher CMake versions)
option(ENABLE_CUDA "Build with CUDA support" OFF)


set(RASTER_TAA_NUM_SAMPLES 16 CACHE STRING
    "Number of sample offsets to use with raster TAA.")

# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20) # For filesystem (17) + designated initializers (20)
get_property(_available_cxx_features GLOBAL PROPERTY CMAKE_CXX_KNOWN_FEATURES)
if ("${_available_cxx_features}" MATCHES "cxx_std_20")
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
else ()
    message(WARNING "No full C++20 compatibility indicated, compiling with non-standard extensions."
        "Available feaures: ${_available_cxx_features}")
endif ()
set(PROJECT_LANGUAGES C CXX)

if (ENABLE_CUDA)
    list(APPEND PROJECT_LANGUAGES CUDA)
    set(CMAKE_CUDA_STANDARD 14) # todo: maximum supported currently?
    #set(CMAKE_CUDA_RUNTIME_LIBRARY Static CACHE INTERNAL "Statically link against selected CUDA runtime" FORCE)
endif ()

# ------------------------------------------------------------------------------

project(rptr ${PROJECT_LANGUAGES})

if (COMPILING_FOR_DG2)
    add_compile_definitions(COMPILING_FOR_DG2)
endif()

if (${FORCE_COLORED_OUTPUT})
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fdiagnostics-color=always>)
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,Clang>:-fcolor-diagnostics>)
    endif ()
endif ()

if (MSVC)
	#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ") # /std:c++latest now set by CXX standard below
else ()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-unused-parameter")
	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|CLANG")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
    endif ()
    if (CMAKE_BUILD_TYPE MATCHES "Debug|DEBUG")
		add_definitions(-D_DEBUG)
	endif ()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Threads REQUIRED)

if (ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    add_definitions(-DENABLE_VULKAN)
    add_definitions(-DRASTER_TAA_NUM_SAMPLES=${RASTER_TAA_NUM_SAMPLES})
endif()


add_subdirectory(ext)

if (WIN32)
    add_definitions(-DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
endif ()


option(ENABLE_REALTIME_RESOLVE "Enable real-time rendering features such as reprojection" OFF)
if (ENABLE_REALTIME_RESOLVE)
    add_compile_definitions(ENABLE_REALTIME_RESOLVE)
endif ()

option(ENABLE_PROFILING_TOOLS "Enable profiling tools" OFF)
if (ENABLE_PROFILING_TOOLS)
    add_compile_definitions(ENABLE_PROFILING_TOOLS)
endif ()

option(ENABLE_EXAMPLES "Enable example codes and functions" OFF)
if (ENABLE_EXAMPLES)
    add_compile_definitions(ENABLE_EXAMPLES)
endif ()

option(ENABLE_DEBUG_VIEWS "Enable debug views" OFF)
if (ENABLE_DEBUG_VIEWS)
    add_compile_definitions(ENABLE_DEBUG_VIEWS)
endif ()

option(ENABLE_RESTIR "Enable ReStir" OFF)
if (ENABLE_RESTIR)
    add_compile_definitions(ENABLE_RESTIR)
endif ()

include(scripts/ide_project_tools.cmake)
add_subdirectory(util)
add_subdirectory(librender)

get_property(CMAKE_GENERATOR_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if (CMAKE_GENERATOR_IS_MULTI_CONFIG)
    set(PROJECT_BINARY_PRODUCTS_DIR ${PROJECT_BINARY_DIR}/${CMAKE_CFG_INTDIR})
else ()
    set(PROJECT_BINARY_PRODUCTS_DIR ${PROJECT_BINARY_DIR})
endif ()
include(rendering/gpu_programs.cmake)

# Add module root directory to all dependent modules
target_include_directories(util INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/unstable>)

add_library(render_backends INTERFACE)

if (ENABLE_VULKAN)
    add_subdirectory(vulkan)
    target_link_libraries(render_backends INTERFACE render_vulkan)
endif()


if (ENABLE_CUDA)
    add_definitions(-DENABLE_CUDA)
    add_subdirectory(cuda)
    target_link_libraries(render_backends INTERFACE render_cuda)
endif ()

add_subdirectory(libapp)

add_executable(rptr imstate.cpp imstate.h main.cpp app.cpp cmdline.cpp)
target_precompile_headers(rptr REUSE_FROM util)
target_link_libraries(rptr PUBLIC libapp render_backends)


# additional tests and tools
add_subdirectory(rendering)

# copy required run-time libraries to binary dir
if (EXT_RUNTIME_LIBRARIES)
    add_custom_target(copy-runtime-libraries ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EXT_RUNTIME_LIBRARIES} ${PROJECT_BINARY_PRODUCTS_DIR}
        DEPENDS ${EXT_RUNTIME_LIBRARIES}
    )
    if (EXT_RUNTIME_DEPENDENCIES)
        add_dependencies(copy-runtime-libraries ${EXT_RUNTIME_DEPENDENCIES})
    endif ()
    add_dependencies(rptr copy-runtime-libraries)
endif ()

# IDE support
set_target_properties(
    rptr PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_main_targets(rptr)
