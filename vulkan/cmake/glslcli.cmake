# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

if (CMAKE_GENERATOR MATCHES "Ninja")
    set(CMAKE_POLICY_DEFAULT_CMP0116 OLD)
else ()
    if (CMAKE_VERSION VERSION_LESS "3.20.6")
        message(FATAL_ERROR "Shader change tracking only supported with Ninja generator!")
    else ()
        cmake_policy(SET CMP0116 NEW)
    endif ()
endif ()

if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_Vulkan_hint_executable_search_paths
            "$ENV{VULKAN_SDK}/Bin"
        )
    else()
        set(_Vulkan_hint_executable_search_paths
            "$ENV{VULKAN_SDK}/Bin32"
        )
    endif()
else()
    set(_Vulkan_hint_executable_search_paths
        "$ENV{VULKAN_SDK}/bin"
    )
endif()

# Older cmake does not find glslc, so we do it ourselves.
if (NOT TARGET Vulkan::glslc AND CMAKE_VERSION VERSION_LESS "3.19")
    find_program(Vulkan_GLSLC_EXECUTABLE
        NAMES glslc
        HINTS
          ${_Vulkan_hint_executable_search_paths}
    )
    mark_as_advanced(Vulkan_GLSLC_EXECUTABLE)
endif()

# Older cmake does not find glslangValidator, so we do it ourselves.
if (NOT TARGET Vulkan::glslangValidator AND CMAKE_VERSION VERSION_LESS "3.21")
    find_program(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE
        NAMES glslangValidator
        HINTS
          ${_Vulkan_hint_executable_search_paths}
    )
    mark_as_advanced(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
endif()

# Note: The vulkan find script may find glslc if it is new enough. However,
#       versions < 3.19 do not find it, and we also want users to be able
#       to overwrite this.
set(GLSLC "${Vulkan_GLSLC_EXECUTABLE}" CACHE FILEPATH
    "Path to the glslc executable.")

execute_process(COMMAND ${GLSLC} --version OUTPUT_VARIABLE GLSLC_VERSION)
if (GLSLC_VERSION MATCHES "glsl|GLSL")
    message(STATUS "Found glslc: ${GLSLC}")
else()
    message(FATAL_ERROR "glslc not accessible, but it is required "
        "for building with Vulkan support (GLSLC=${GLSLC}).")
endif()

# Note that the include paths and defines should not have
# the -I or -D prefix, respectively
function(glslc_command_line CMD_LINE_OUT GLSLC_VARIANT)
    set(multi_options INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS)
    cmake_parse_arguments(PARSE_ARGV 2 GLSLC "" "" "${multi_options}")

    # store options for reuse by alternative compilers
    foreach (option ${multi_options})
        set(GLSLC_${option} ${GLSLC_${option}} PARENT_SCOPE)
    endforeach()

    glslc_rebuild_command_line(_cmd_line ${GLSLC_VARIANT})
    set(${CMD_LINE_OUT} ${_cmd_line} PARENT_SCOPE)
endfunction()

function(glslc_rebuild_command_line CMD_LINE_OUT GLSLC_VARIANT)
    set(GLSLC ${${GLSLC_VARIANT}})
    set(GLSLC_VERSION ${${GLSLC_VARIANT}_VERSION})

    set(RELOCATABLE_INCLUDE_DIRECTORIES "")
    foreach (inc ${GLSLC_INCLUDE_DIRECTORIES})
        if (NOT IS_ABSOLUTE ${inc})
            if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.19")
                file(REAL_PATH "${inc}" inc EXPAND_TILDE)
            else ()
                set(inc "${CMAKE_CURRENT_SOURCE_DIR}/${inc}")
            endif ()
        endif ()

        file(RELATIVE_PATH inc "${PROJECT_SOURCE_DIR}" "${inc}")
        file(TO_NATIVE_PATH "\${SOURCE_DIR}/${inc}" native_path)
        list(APPEND RELOCATABLE_INCLUDE_DIRECTORIES "-I${native_path}")
    endforeach()

    set(GLSLC_DEFN_ARGS "")
    foreach (def ${GLSLC_COMPILE_DEFINITIONS})
        list(APPEND GLSLC_ARGS "-D${def}")
    endforeach()

    set(GLSLC_ARGS
        --target-env=vulkan1.2
        ${GLSLC_COMPILE_OPTIONS}
        ${ARGN}
        )

    if (GLSLC_VERSION MATCHES "shaderc")
        set(GLSLC_ARGS ${GLSLC_ARGS}
            -MD -MF \${DEP_FILE}
            )
    else ()
        list(TRANSFORM GLSLC_ARGS REPLACE "=" ";")
        list(TRANSFORM GLSLC_ARGS REPLACE "-O$" "")
        set(GLSLC_ARGS ${GLSLC_ARGS}
            --quiet # monitor: note: preserves error reporting
            -V -l
            --depfile \${DEP_FILE}
            )
    endif ()

    get_filename_component(GLSLC_RELEASE_NAME "${GLSLC}" NAME)
    if (GLSLC_VARIANT MATCHES "GLSLC_(.*)")
        string(TOLOWER ${CMAKE_MATCH_1} CMAKE_MATCH_1)

        # ugh, embed new name in global command line with 0 generator expression
        get_filename_component(GLSLC "${GLSLC}" DIRECTORY)
        set(GLSLC ${GLSLC}/$<0:${CMAKE_MATCH_1}->${GLSLC_RELEASE_NAME})

        set(GLSLC_RELEASE_NAME ${CMAKE_MATCH_1}-${GLSLC_RELEASE_NAME})
    endif ()

    set(${CMD_LINE_OUT}
        ${GLSLC} ${GLSLC_DEFN_ARGS} ${GLSLC_ARGS} ${RELOCATABLE_INCLUDE_DIRECTORIES}
        PARENT_SCOPE)

    set(GLSLC_RELEASE ${PROJECT_BINARY_PRODUCTS_DIR}/${GLSLC_RELEASE_NAME})
    add_custom_command(
        OUTPUT ${GLSLC_RELEASE}
        COMMAND ${CMAKE_COMMAND} -E copy ${GLSLC} ${GLSLC_RELEASE}
        DEPENDS ${GLSLC})
    add_custom_target(${GLSLC_VARIANT}-runtime-tool ALL DEPENDS ${GLSLC_RELEASE})

    if (NOT TARGET glslc-runtime-tools)
        add_custom_target(glslc-runtime-tools ALL)
    endif ()
    add_dependencies(glslc-runtime-tools ${GLSLC_VARIANT}-runtime-tool)
endfunction()
