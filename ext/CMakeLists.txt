# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

set(CMAKE_FOLDER "external") # IDE filters

include(libvkr/ext/ext.cmake NO_POLICY_SCOPE)
include(ExternalProject)

set(FETCHCONTENT_QUIET ${FETCHCONTENT_QUIET} PARENT_SCOPE)

# ------------------------------------------------------------------------------

set(SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/download")
set(DOWNLOAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/download")

# ------------------------------------------------------------------------------

if (NOT RPTR_GLM_SRC)
  set(_RPTR_GLM_SRC
      "https://github.com/g-truc/glm/releases/download/0.9.9.8/glm-0.9.9.8.zip")
  set(_RPTR_GLM_HASH SHA256=37e2a3d62ea3322e43593c34bae29f57e3e251ea89f4067506c94043769ade4c)
  set(RPTR_GLM_SRC ""
    CACHE STRING "The source URL for glm. This can be a local file. E.g. ${_RPTR_GLM_SRC}")
  set(RPTR_GLM_HASH ""
    CACHE STRING "The source archive hash for glm. E.g. ${_RPTR_GLM_HASH}")
  materialize_local_defaults(RPTR_GLM_SRC RPTR_GLM_HASH)
endif ()

if (NOT RPTR_GLFW_SRC)
  set(_RPTR_GLFW_SRC
      "https://github.com/glfw/glfw/releases/download/3.3.7/glfw-3.3.7.zip")
  set(_RPTR_GLFW_HASH SHA256=4ef0c544a8ace9a6cd0e0aef8250090f89fea1bf96e9fc1d9d6f76386c290c9c)
  set(RPTR_GLFW_SRC ""
    CACHE STRING "The source URL for glfw. This can be a local file. E.g. ${_RPTR_GLFW_SRC}")
  set(RPTR_GLFW_HASH ""
    CACHE STRING "The source archive hash for glfw. E.g. ${_RPTR_GLFW_HASH}")
  materialize_local_defaults(RPTR_GLFW_SRC RPTR_GLFW_HASH)
endif ()

if (NOT RPTR_DPCPP_SRC)
  if (WIN32)
    set(_RPTR_DPCPP_SRC
      "https://github.com/intel/llvm/releases/download/sycl-nightly%2F20221215/dpcpp-compiler-win.tar.gz")
    set(_RPTR_DPCPP_HASH
      SHA256=09efbb0e80337efc1218a51e8b505abab8fda8bb7ce9874092fd8d38139b3874)
  else ()
    set(_RPTR_DPCPP_SRC
      "https://github.com/intel/llvm/releases/download/sycl-nightly%2F20221215/dpcpp-compiler.tar.gz")
    set(_RPTR_DPCPP_HASH
      "SHA256=c0af9fcdfd3bcbe957fbaf1ccc3f1a6447d41d681e53c578e338ea0c52c21b6e")
  endif ()
  set(RPTR_DPCPP_SRC ""
    CACHE STRING "The source URL for DPC++. This can be a local file. E.g. ${_RPTR_DPCPP_SRC}")
  set(RPTR_DPCPP_HASH ""
    CACHE STRING "The source archive hash for DPC++. E.g. ${_RPTR_DPCPP_HASH}")
  materialize_local_defaults(RPTR_DPCPP_SRC RPTR_DPCPP_HASH)
endif ()

if (NOT RPTR_OIDN2_SRC AND FALSE) # currently not using binary release yet
  if (WIN32)
    set(_RPTR_OIDN2_SRC
      "https://github.com/OpenImageDenoise/oidn/releases/download/v2.0.0/oidn-2.0.0.x64.windows.zip")
    set(_RPTR_OIDN2_HASH
      SHA256=df0c6bf8185f9736dd10eb07e4bf1c68c6ef28c0cddd42a9555f9e8435a24e40)
  else ()
    set(_RPTR_OIDN2_SRC
      "https://github.com/OpenImageDenoise/oidn/releases/download/v2.0.0/oidn-2.0.0.x86_64.linux.tar.gz")
    set(_RPTR_OIDN2_HASH
      SHA256=8de608405ab4e687a154560245a4520cf44a5a917f7fa2ec2b53e885638e2865)
  endif ()
  set(RPTR_OIDN2_SRC ""
    CACHE STRING "The source URL for Intel OpenImageDenoise 2. This can be a local file. E.g. ${_RPTR_OIDN2_SRC}")
  set(RPTR_OIDN2_HASH ""
    CACHE STRING "The source archive hash for Intel OpenImageDenoise 2. E.g. ${_RPTR_OIDN2_HASH}")
    materialize_local_defaults(RPTR_OIDN2_SRC RPTR_OIDN2_HASH)
endif ()


if (NOT RPTR_CRYPTO_SRC)
    set(_RPTR_CRYPTO_SRC "https://github.com/B-Con/crypto-algorithms")
    set(_RPTR_CRYPTO_HASH "cfbde48414baacf51fc7c74f275190881f037d32")
    set(RPTR_CRYPTO_SRC ""
        CACHE STRING "The source URL for B-Con/crypto-algorithms. This must be a git repository. E.g. ${_RPTR_CRYPTO_SRC}")
    set(RPTR_CRYPTO_HASH "The source git hash for B-Con/crypto-algorithms. E.g. ${_RPTR_CRYPTO_HASH}")
    materialize_local_defaults(RPTR_CRYPTO_SRC RPTR_CRYPTO_HASH)
endif()

if (NOT RPTR_TINYEXR_SRC)
  set(_RPTR_TINYEXR_SRC
    "https://github.com/syoyo/tinyexr/archive/refs/tags/v1.0.7.zip")
  set(_RPTR_TINYEXR_HASH
    SHA256=0744f3e3fb6192141c0466facbfdc8dd27e1d71602406d60289251003a790559)
  set(RPTR_TINYEXR_SRC ""
    CACHE STRING "The source archive for tinyexr. E.g. ${_RPTR_TINYEXR_SRC}")
  set(RPTR_TINYEXR_HASH ""
    CACHE STRING "The source archive hash for tinyexr. E.g. ${_RPTR_TINYEXR_HASH}")
  materialize_local_defaults(RPTR_TINYEXR_SRC RPTR_TINYEXR_HASH)
endif ()

if (NOT RPTR_IMGUI_SRC)
  set(_RPTR_IMGUI_SRC
    "https://github.com/ocornut/imgui/archive/refs/tags/v1.89.9.zip")
  set(_RPTR_IMGUI_HASH
    SHA256=28772c1155aebd71f818ae43e29427571c4faa5e9cdda3b44b1d2c2dc64297f5)
  set(RPTR_IMGUI_SRC ""
    CACHE STRING "The source archive for imgui. E.g. ${_RPTR_IMGUI_SRC}")
  set(RPTR_IMGUI_HASH ""
    CACHE STRING "The source archive hash for imgui. E.g. ${_RPTR_IMGUI_HASH}")
  materialize_local_defaults(RPTR_IMGUI_SRC RPTR_IMGUI_HASH)
endif ()

if (NOT RPTR_STB_SRC)
  set(_RPTR_STB_SRC "https://github.com/nothings/stb")
  set(_RPTR_STB_HASH "5736b15f7ea0ffb08dd38af21067c314d6a3aae9")
  set(RPTR_STB_SRC ""
    CACHE STRING "The source URL for stb. This must be a git repository. E.g. ${_RPTR_STB_SRC}")
  set(RPTR_STB_HASH ""
    CACHE STRING "The source git hash for stb. E.g. ${_RPTR_STB_HASH}")
  materialize_local_defaults(RPTR_STB_SRC RPTR_STB_HASH)
endif ()

# GLM --------------------------------------------------------------------------
FetchContent_Declare(glm
  URL ${RPTR_GLM_SRC}
  URL_HASH ${RPTR_GLM_HASH}
  SOURCE_DIR    ${SOURCE_ROOT}/glm/src
  DOWNLOAD_DIR  ${DOWNLOAD_ROOT}/glm
)

set(BUILD_SHARED_LIBS OFF)
set(GLM_TESTS_ENABLE OFF)
FetchContent_MakeAvailable(glm)
if (NOT EXISTS ${glm_SOURCE_DIR}/glm/glm.hpp)
  message(FATAL_ERROR "glm directory needs to be updated, please rebuild (or remove _deps in build folder)")
endif ()

set(GLM_INTERFACE_DEFINITIONS -DGLM_ENABLE_EXPERIMENTAL PARENT_SCOPE)
#target_compile_definitions(glm INTERFACE -DGLM_ENABLE_EXPERIMENTAL)

# GLFW -------------------------------------------------------------------------
FetchContent_Declare(glfw
  URL ${RPTR_GLFW_SRC}
  URL_HASH ${RPTR_GLFW_HASH}
  SOURCE_DIR    ${SOURCE_ROOT}/glfw/src
  DOWNLOAD_DIR  ${DOWNLOAD_ROOT}/glfw
)

set(BUILD_SHARED_LIBS OFF)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
FetchContent_MakeAvailable(glfw)
if (NOT EXISTS ${glfw_SOURCE_DIR}/CMakeLists.txt)
  message(FATAL_ERROR "glfw directory needs to be updated, please rebuild (or remove _deps in build folder)")
endif ()

set(GLFW_INTERFACE_DEFINITIONS $<BUILD_INTERFACE:${glfw_SOURCE_DIR}/include> PARENT_SCOPE)
#target_include_directories(glfw
#    INTERFACE $<BUILD_INTERFACE:${glfw_SOURCE_DIR}/include>)

# SHA1 / B-Con crypto-algorithms. ----------------------------------------------

fetch_git_repository(
  NAME crypto_algorithms
  GIT_REPOSITORY ${RPTR_CRYPTO_SRC}
  GIT_HASH       ${RPTR_CRYPTO_HASH}
  SOURCE_DIR     ${SOURCE_ROOT}/crypto_algorithms/src
  DOWNLOAD_DIR   ${DOWNLOAD_ROOT}/crypto_algorithms
)
populate_git_repository(NAME crypto_algorithms)

if (NOT EXISTS ${crypto_algorithms_SOURCE_DIR}/sha1.c)
  message(FATAL_ERROR "crypto_algorithms directory needs to be updated, please rebuild (or remove _deps in build folder)")
endif ()

add_library(crypto-algorithms STATIC ${crypto_algorithms_SOURCE_DIR}/sha1.c)
target_include_directories(crypto-algorithms PUBLIC $<BUILD_INTERFACE:${crypto_algorithms_SOURCE_DIR}>)

# stb --------------------------------------------------------------------------

fetch_git_repository(
  NAME stb
  GIT_REPOSITORY ${RPTR_STB_SRC}
  GIT_HASH       ${RPTR_STB_HASH}
  SOURCE_DIR     ${SOURCE_ROOT}/stb/src
  DOWNLOAD_DIR   ${DOWNLOAD_ROOT}/stb
)
populate_git_repository(NAME stb)

add_library(stb STATIC stb/stb.cpp)
target_include_directories(stb PUBLIC $<BUILD_INTERFACE:${stb_SOURCE_DIR}>)

# tinyexr ----------------------------------------------------------------------

FetchContent_Declare(
  tinyexr
  URL           ${RPTR_TINYEXR_SRC}
  URL_HASH      ${RPTR_TINYEXR_HASH}
  SOURCE_DIR    ${SOURCE_ROOT}/tinyexr/src
  DOWNLOAD_DIR  ${DOWNLOAD_ROOT}/tinyexr
)
FetchContent_Populate(tinyexr)

add_library(tinyexr STATIC ${tinyexr_SOURCE_DIR}/tinyexr.cc)
target_compile_definitions(tinyexr PUBLIC TINYEXR_USE_MINIZ=0 TINYEXR_USE_STB_ZLIB=1)
target_include_directories(tinyexr PUBLIC $<BUILD_INTERFACE:${tinyexr_SOURCE_DIR}>)
target_link_libraries(tinyexr PUBLIC stb)

# OpenImageDenoise --------------------------------------------------------------------
FetchContent_Declare(
  DPCpp
  URL           ${RPTR_DPCPP_SRC}
  URL_HASH      ${RPTR_DPCPP_HASH}
  SOURCE_DIR    ${SOURCE_ROOT}/dpcpp/src
  DOWNLOAD_DIR  ${DOWNLOAD_ROOT}/dpcpp
)

if (ENABLE_OIDN2)
  FetchContent_Populate(DPCpp)
  if (WIN32)
      message("Note: Long paths in submodules may require: git config --global core.longpaths true")
  endif ()

  fetch_git_repository(
    NAME OpenImageDenoise2
    GIT_REPOSITORY https://github.com/OpenImageDenoise/oidn
    GIT_TAG        origin/master
    GIT_SUBMODULES "weights" # "external/cutlass"
    SOURCE_DIR     ${SOURCE_ROOT}/OpenImageDenoise2/src
    DOWNLOAD_DIR   ${DOWNLOAD_ROOT}/OpenImageDenoise2
  )
  populate_git_repository(NAME OpenImageDenoise2)

  # Force configure at configure time to catch errors early
  if (WIN32)
    set(ENV{INCLUDE} "${dpcpp_SOURCE_DIR}/include;$ENV{INCLUDE}")
    set(ENV{LIB} "${dpcpp_SOURCE_DIR}/lib;$ENV{LIB}")
  endif ()
  execute_process(COMMAND ${CMAKE_COMMAND} -G Ninja
    -S ${openimagedenoise2_SOURCE_DIR}
    -B ${openimagedenoise2_BINARY_DIR}
    -D CMAKE_C_COMPILER=${dpcpp_SOURCE_DIR}/bin/clang.exe
    -D CMAKE_CXX_COMPILER=${dpcpp_SOURCE_DIR}/bin/clang.exe
    -D OIDN_DEVICE_CPU=OFF
    -D OIDN_DEVICE_SYCL=ON
    -D OIDN_APPS=OFF
    -D OIDN_DEVICE_SYCL_AOT=OFF
    -D CMAKE_BUILD_TYPE=Release
    )
  ExternalProject_Add(OpenImageDenoise2-build
    SOURCE_DIR         ${openimagedenoise2_SOURCE_DIR}
    BINARY_DIR         ${openimagedenoise2_BINARY_DIR}
    CONFIGURE_COMMAND  ${CMAKE_COMMAND} ${openimagedenoise2_BINARY_DIR}
    BUILD_COMMAND      ${CMAKE_COMMAND} --build ${openimagedenoise2_BINARY_DIR}
    BUILD_ALWAYS       TRUE
    BUILD_BYPRODUCTS   ${openimagedenoise2_BINARY_DIR}/OpenImageDenoise${CMAKE_SHARED_LIBRARY_SUFFIX}
                       ${openimagedenoise2_BINARY_DIR}/OpenImageDenoise_core${CMAKE_SHARED_LIBRARY_SUFFIX}
                       ${openimagedenoise2_BINARY_DIR}/OpenImageDenoise_device_sycl${CMAKE_SHARED_LIBRARY_SUFFIX}
    INSTALL_COMMAND    ""
    )
  # Add wrapper library to link to
  add_library(OpenImageDenoise2-dpcpp INTERFACE)
  add_dependencies(OpenImageDenoise2-dpcpp OpenImageDenoise2-build)
  target_include_directories(OpenImageDenoise2-dpcpp INTERFACE ${openimagedenoise2_SOURCE_DIR}/include)
  target_link_directories(OpenImageDenoise2-dpcpp INTERFACE ${openimagedenoise2_BINARY_DIR})
  target_link_libraries(OpenImageDenoise2-dpcpp INTERFACE OpenImageDenoise)
  list(APPEND EXT_RUNTIME_LIBRARIES
    ${openimagedenoise2_BINARY_DIR}/OpenImageDenoise${CMAKE_SHARED_LIBRARY_SUFFIX}
    ${openimagedenoise2_BINARY_DIR}/OpenImageDenoise_core${CMAKE_SHARED_LIBRARY_SUFFIX}
    ${openimagedenoise2_BINARY_DIR}/OpenImageDenoise_device_sycl${CMAKE_SHARED_LIBRARY_SUFFIX}
    ${dpcpp_SOURCE_DIR}/bin/sycl6${CMAKE_SHARED_LIBRARY_SUFFIX}
    ${dpcpp_SOURCE_DIR}/bin/pi_level_zero${CMAKE_SHARED_LIBRARY_SUFFIX}
    )
  list(APPEND EXT_RUNTIME_DEPENDENCIES OpenImageDenoise2-dpcpp)
endif ()


# imgui ------------------------------------------------------------------------

FetchContent_Declare(imgui
  URL ${RPTR_IMGUI_SRC}
  URL_HASH ${RPTR_IMGUI_HASH}
  SOURCE_DIR    ${SOURCE_ROOT}/imgui/src
  DOWNLOAD_DIR  ${DOWNLOAD_ROOT}/imgui
)

FetchContent_Populate(imgui)

set(IMGUI_SOURCES
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)

if (ENABLE_VULKAN)
    set(IMGUI_SOURCES ${IMGUI_SOURCES} ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
endif()

add_library(imgui STATIC ${IMGUI_SOURCES})
if (ENABLE_VULKAN)
    target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
endif()

target_include_directories(imgui PUBLIC
  $<BUILD_INTERFACE:${imgui_SOURCE_DIR}>)

# others -----------------------------------------------------------------------

add_subdirectory(glad)

# libvkr------------------------------------------------------------------------

set(LIBVKR_ENABLE_PYTHON ${ENABLE_PYTHON} CACHE BOOL "Enable Python integration" FORCE)
add_subdirectory(libvkr)

# ------------------------------------------------------------------------------

set(EXT_RUNTIME_LIBRARIES ${EXT_RUNTIME_LIBRARIES} PARENT_SCOPE)
set(EXT_RUNTIME_DEPENDENCIES ${EXT_RUNTIME_DEPENDENCIES} PARENT_SCOPE)

# IDE filters
unset(CMAKE_FOLDER)
set_property(TARGET vkr PROPERTY FOLDER ${CMAKE_FOLDER})
