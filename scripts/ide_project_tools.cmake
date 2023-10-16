# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

if (CMAKE_GENERATOR MATCHES "Visual Studio|Xcode")
    set(CMAKE_GENERATES_IDE_PROJECT true)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
else ()
    set(CMAKE_GENERATES_IDE_PROJECT false)
endif ()

function(add_project_files target directory) # takes patterns as ARGN
    if (NOT CMAKE_GENERATES_IDE_PROJECT)
        return ()
    endif ()
    set(_single_options SOURCE_GROUP)
    cmake_parse_arguments(_project_files "" "${_single_options}" "" "${ARGN}")
    set(patterns ${_project_files_UNPARSED_ARGUMENTS})
    list(TRANSFORM patterns PREPEND ${directory}/)
    #message(${patterns})
    file(GLOB_RECURSE headers
        RELATIVE_PATH ${directory}
        FOLLOW_SYMLINKS true
        ${patterns})
    #message("${headers}")
    target_sources(${target} PRIVATE ${headers})
    if (_project_files_SOURCE_GROUP)
        source_group(TREE ${directory} PREFIX ${_project_files_SOURCE_GROUP} FILES ${headers})
    endif ()
endfunction()

function(set_main_targets) # marks any other targets as support targets
    if (NOT CMAKE_GENERATES_IDE_PROJECT)
        return ()
    endif ()
    if (CMAKE_FOLDER STREQUAL "support")
        message(FATAL_ERROR "Default CMAKE_FOLDER was set to \"support\" while setting main targets")
    endif ()
    set(main_targets ${ARGN})
    get_property(target_names DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
    foreach (target ${target_names})
        if (target IN_LIST main_targets)
            set_property(TARGET ${target} PROPERTY FOLDER ${CMAKE_FOLDER})
        else ()
            if (target MATCHES "^test_")
                set_target_properties(${target} PROPERTIES FOLDER "test")
            else ()
                message(${target})
                set_target_properties(${target} PROPERTIES FOLDER "support")
            endif ()
        endif ()
    endforeach ()
endfunction()

macro(begin_support_targets)
    set(CMAKE_FOLDER "support")
endmacro()
macro(end_support_targets)
    unset(CMAKE_FOLDER)
endmacro()
