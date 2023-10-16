# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

include(FetchContent)

set(FETCHCONTENT_QUIET FALSE)
set(FETCHCONTENT_QUIET FALSE PARENT_SCOPE)

if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    # Re: warning <DOWNLOAD_EXTRACT_TIMESTAMP>
    cmake_policy(SET CMP0135 NEW)
endif()


# work around old CACHE variable policy unbinding local variables
macro (materialize_local_defaults)
  foreach (_var ${ARGN})
    set(${_var} ${_${_var}})
  endforeach ()
endmacro ()

# Using FetchContent with git repositories has proven to be dangerous
# in some circumstances. In particular, the parent repository can be changed
# when the original clone is missing.
# We prevent this here, at the cost of making re-fetches a bit more complicated.
macro (fetch_git_repository)
  set(ONE_VALUE NAME GIT_REPOSITORY GIT_TAG GIT_HASH SOURCE_DIR DOWNLOAD_DIR)
  set(MULTI_VALUE GIT_SUBMODULES)
  cmake_parse_arguments(ARG "${NO_VALUE}" "${ONE_VALUE}" "${MULTI_VALUE}" ${ARGN})
  set(${ARG_NAME}_SOURCE_DIRECTORY "${ARG_SOURCE_DIR}")
  if (EXISTS ${${ARG_NAME}_SOURCE_DIRECTORY}/.git)
    set(${ARG_NAME}_FETCH_ARGS
      DOWNLOAD_COMMAND
        echo "Found existing git checkout. Skipping ${ARG_NAME} clone and update to avoid override."
    )
  else ()
    if (${ARG_NAME}_FETCHED AND NOT EXISTS "${ARG_SOURCE_DIR}/.git")
      # danger: do not automatically re-fetch, in some build systems it starts
      # overwriting the parent repository when the original clone is missing
      message(FATAL_ERROR "Missing clone of ${ARG_NAME} in ext."
        "To re-clone automatically, please remove related directories in \"${PROJECT_BINARY_DIR}/_deps\""
        "and then set the CMake cache variable ${ARG_NAME}_FETCHED=0")
    endif ()
    if (NOT "${ARG_GIT_TAG}" STREQUAL "")
      set(GIT_CLONE_REF ${ARG_GIT_TAG})
      set(GIT_CLONE_SHALLOW TRUE)
      message(STATUS "Found GIT_TAG for ${ARG_NAME}, will ignore GIT_HASH and perform shallow clone.")
      message(STATUS "This can fail if the tag is actually a hash.")
    else ()
      # Cannot do shallow clones with a hash,
      # see https://cmake.org/cmake/help/v3.21/module/ExternalProject.html
      message(STATUS "Did not find GIT_TAG for ${ARG_NAME}, will perform deep clone with GIT_HASH.")
      set(GIT_CLONE_REF "${ARG_GIT_HASH}")
      set(GIT_CLONE_SHALLOW FALSE)
  endif()
    set(${ARG_NAME}_FETCH_ARGS
      GIT_REPOSITORY "${ARG_GIT_REPOSITORY}"
      GIT_TAG        "${GIT_CLONE_REF}"
      GIT_SUBMODULES "${ARG_GIT_SUBMODULES}"
      GIT_SHALLOW    ${GIT_CLONE_SHALLOW}
      GIT_PROGRESS   TRUE
    )
  endif ()
  FetchContent_Declare(
    ${ARG_NAME}
    ${${ARG_NAME}_FETCH_ARGS}
    SOURCE_DIR "${ARG_SOURCE_DIR}"
    DOWNLOAD_DIR "${ARG_DOWNLOAD_DIR}"
  )
endmacro()

macro (populate_git_repository)
  set(NO_VALUE "")
  set(ONE_VALUE NAME)
  set(MULTI_VALUE "")
  cmake_parse_arguments(ARG "${NO_VALUE}" "${ONE_VALUE}" "${MULTI_VALUE}" ${ARGN})
  FetchContent_Populate(${ARG_NAME})
  set(${ARG_NAME}_FETCHED TRUE CACHE BOOL "Prevent broken re-fetching with missing clones" FORCE)
endmacro()
