# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.16.3)

option(LIBVKR_ENABLE_TOOLS "Enable tools module for mesh processing" ON)
option(LIBVKR_ENABLE_PYTHON "Enable module for using libvkr in Python" ON)

if (LIBVKR_ENABLE_PYTHON AND NOT LIBVKR_ENABLE_TOOLS)
  set(LIBVKR_ENABLE_TOOLS ON CACHE "Tools are built when python is enabled" FORCE)
endif ()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

project(libvkr VERSION 1.0 LANGUAGES C CXX)

# Uncomment to compile with address sanitizer.
# set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
# set(CMAKE_C_COMPILER clang)

add_library(vkr STATIC src/vkr.c)
target_include_directories(vkr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# On some systems, we need to link against libm to use pow.
# Only do that if libm exists, though.
include(CheckLibraryExists)
check_library_exists(m pow "" NEED_LIBM)
if (NEED_LIBM)
  target_link_libraries(vkr PUBLIC m)
endif()

if (LIBVKR_ENABLE_TOOLS)
  add_subdirectory(ext)

  add_library(vkr_tools STATIC src/vkr.c)
  target_include_directories(vkr_tools PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_compile_definitions(vkr_tools PRIVATE VKR_BUILD_TOOLS)
  if (NEED_LIBM)
    target_link_libraries(vkr_tools PUBLIC m)
  endif()
  target_link_libraries(vkr_tools PRIVATE meshoptimizer vkr_stb)

  add_executable(vkrtest src/vkrtest.c)
  target_link_libraries(vkrtest PRIVATE vkr_tools)

  add_executable(vktconvert src/vktconvert.c)
  target_link_libraries(vktconvert PRIVATE vkr_tools)

  ## The python module is an optional component, but we require it
  ## for full functionality in our conversion utilities.
  if (LIBVKR_ENABLE_PYTHON)
    add_library(pyvkr MODULE src/vkrmodule.c)
    target_link_libraries(pyvkr PRIVATE vkr_python vkr_tools)
    set_property(TARGET pyvkr PROPERTY PREFIX "")
    if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
      set_property(TARGET pyvkr PROPERTY SUFFIX ".pyd")
      set_property(TARGET pyvkr PROPERTY OUTPUT_NAME "pyvkr$<$<CONFIG:Debug>:_d>")
    endif()
  endif ()
endif ()

# requires pyvkr TARGET for full functionality
add_subdirectory(scripts/blender_vkr)
