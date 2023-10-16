# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

include(ext.cmake NO_POLICY_SCOPE)

set(VKR_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/download")
set(VKR_DOWNLOAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/download")


# meshoptimizer ----------------------------------------------------------------

# Note: We build our own library here to make it small, and to ensure that we
#       do not link against all of C++ inadvertently.
add_library(meshoptimizer STATIC
    meshoptimizer-0.18/src/vcacheoptimizer.cpp
    meshoptimizer-0.18/src/vfetchoptimizer.cpp
    meshoptimizer-0.18/src/indexgenerator.cpp
)
target_include_directories(meshoptimizer PUBLIC meshoptimizer-0.18/src)

# stb --------------------------------------------------------------------------

if (NOT VKR_STB_SRC)
  set(_VKR_STB_SRC "https://github.com/nothings/stb")
  set(_VKR_STB_HASH "5736b15f7ea0ffb08dd38af21067c314d6a3aae9")
  set(VKR_STB_SRC ""
    CACHE STRING "The source URL for stb. This must be a git repository. E.g. ${_VKR_STB_SRC}")
  set(RPTR_STB_HASH ""
    CACHE STRING "The source git hash for stb. E.g. ${_VKR_STB_HASH}")
  materialize_local_defaults(VKR_STB_SRC VKR_STB_HASH)
endif ()

fetch_git_repository(
  NAME vkr_stb
  GIT_REPOSITORY ${VKR_STB_SRC}
  GIT_HASH ${VKR_STB_HASH}
  SOURCE_DIR ${VKR_SOURCE_ROOT}/vkr_stb/src
  DOWNLOAD_DIR ${VKR_SOURCE_ROOT}/vkr_stb
)
populate_git_repository(NAME vkr_stb)

add_library(vkr_stb INTERFACE)
target_include_directories(vkr_stb INTERFACE $<BUILD_INTERFACE:${vkr_stb_SOURCE_DIR}>)

# python -----------------------------------------------------------------------

add_library(vkr_python INTERFACE)
target_include_directories(vkr_python INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/numpy-1.21.0/include"
)

if (WIN32)
    set(_VKR_PYTHON_VERSION 3.10.6)
    if (NOT VKR_PYTHON_SRC)
      set(_VKR_PYTHON_SRC "https://www.python.org/ftp/python/${_VKR_PYTHON_VERSION}/amd64/dev.msi")
      set(_VKR_PYTHON_HASH SHA256=3ad7cd0a59672e94344d4a45a9ebd09f20fc176a831b1186178cffdb635c4263)
      set(VKR_PYTHON_SRC ""
        CACHE STRING "The source URL for Python ${_VKR_PYTHON_VERSION}. This must be called dev.msi, e.g. ${_VKR_PYTHON_SRC}")
      set(RPTR_PYTHON_HASH ""
        CACHE STRING "The source hash for Python. E.g. ${_VKR_PYTHON_HASH}")
      materialize_local_defaults(VKR_PYTHON_SRC VKR_PYTHON_HASH)
    endif ()

    FetchContent_Declare(python-release
      URL ${VKR_PYTHON_SRC}
      URL_HASH ${VKR_PYTHON_HASH}
      SOURCE_DIR    ${VKR_SOURCE_ROOT}/python/src
      DOWNLOAD_DIR  ${VKR_DOWNLOAD_ROOT}/python
      DOWNLOAD_NO_EXTRACT TRUE
      PATCH_COMMAND powershell -ExecutionPolicy Bypass
                               -File "${CMAKE_CURRENT_SOURCE_DIR}/extract_msi.ps1"
                               "${VKR_DOWNLOAD_ROOT}/python/dev.msi"
                               "${VKR_SOURCE_ROOT}/python/src"
    )
    FetchContent_MakeAvailable(python-release)

    if (NOT VKR_PYTHON_DEBUG_SRC)
      set(_VKR_PYTHON_DEBUG_SRC "https://www.python.org/ftp/python/${_VKR_PYTHON_VERSION}/amd64/dev_d.msi")
      set(_VKR_PYTHON_DEBUG_HASH SHA256=6df99721a48e1ba4a019889864c7cda4ea41cd0a155d150ef48d657fc09724c0)
      set(VKR_PYTHON_DEBUG_SRC ""
        CACHE STRING "The source URL for Python ${_VKR_PYTHON_VERSION}. This must be called dev_d.msi, e.g. ${_VKR_PYTHON_DEBUG_SRC}")
      set(RPTR_PYTHON_DEBUG_HASH ""
        CACHE STRING "The source hash for Python. E.g. ${_VKR_PYTHON_DEBUG_HASH}")
      materialize_local_defaults(VKR_PYTHON_DEBUG_SRC VKR_PYTHON_DEBUG_HASH)
    endif ()

    FetchContent_Declare(python-debug
      URL ${VKR_PYTHON_DEBUG_SRC}
      URL_HASH ${VKR_PYTHON_DEBUG_HASH}
      SOURCE_DIR    ${VKR_SOURCE_ROOT}/python/src
      DOWNLOAD_DIR  ${VKR_DOWNLOAD_ROOT}/python
      DOWNLOAD_NO_EXTRACT TRUE
      PATCH_COMMAND powershell -ExecutionPolicy Bypass
                               -File "${CMAKE_CURRENT_SOURCE_DIR}/extract_msi.ps1"
                               "${VKR_DOWNLOAD_ROOT}/python/dev_d.msi"
                               "${VKR_SOURCE_ROOT}/python/src"
    )
    FetchContent_MakeAvailable(python-debug)

    target_include_directories(vkr_python INTERFACE
        "${VKR_SOURCE_ROOT}/python/src/include"
    )
    target_link_libraries(vkr_python INTERFACE optimized
        "${VKR_SOURCE_ROOT}/python/src/libs/python310.lib"
    )
    target_link_libraries(vkr_python INTERFACE debug
        "${VKR_SOURCE_ROOT}/python/src/libs/python310_d.lib"
    )
else()
    target_include_directories(vkr_python INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/python-3.10.6/linux/include"
    )
endif()
