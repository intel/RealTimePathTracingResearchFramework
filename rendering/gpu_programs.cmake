# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

macro(add_gpu_program _name _type _label)
  set(_multi_options COMPILE_DEFINITIONS INHERITS PRECOMPILE_OPTIONS)
  cmake_parse_arguments(_added_gpu_program "" "FUNCTION" "${_multi_options}" "${ARGN}")

  set(RPTR_GPU_PROGRAM_${_name}_TYPE ${_type})
  set(RPTR_GPU_PROGRAM_${_name}_LABEL ${_label})
  set(RPTR_GPU_PROGRAM_${_name}_INHERITS ${_added_gpu_program_INHERITS})
  set(RPTR_GPU_PROGRAM_${_name}_DEFINES ${_added_gpu_program_COMPILE_DEFINITIONS})
  set(RPTR_GPU_PROGRAM_${_name}_OPTIONS ${_added_gpu_program_PRECOMPILE_OPTIONS})
  set(RPTR_GPU_PROGRAM_${_name}_MODULE_NAME ${_added_gpu_program_FUNCTION})
  list(APPEND RPTR_GPU_PROGRAMS "${_name}")
endmacro()

macro(add_computational_raytracer _name _label)
  set(_arg_list "${ARGN}")
  add_gpu_program("${_name}" RAYTRACING "${_label}" ${_arg_list})
  list(APPEND RPTR_COMPUTATIONAL_RAYTRACERS "${_name}")
endmacro()

macro(add_integrator _name _label)
  set(_single_options INTEGRATOR_TYPE)
  cmake_parse_arguments(_gpu_integrator "" "${_single_options}" "" "${ARGN}")

  if (NOT _gpu_integrator_INTEGRATOR_TYPE)
    set(_gpu_integrator_INTEGRATOR_TYPE RAYTRACING)
  endif ()
  add_gpu_program("${_name}" ${_gpu_integrator_INTEGRATOR_TYPE} "${_label}" ${_gpu_integrator_UNPARSED_ARGUMENTS})

  list(APPEND RPTR_INTEGRATORS "${_name}")
endmacro()

macro(set_integrator_type _name _type)
  set(RPTR_GPU_PROGRAM_${_name}_TYPE ${_type})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_FEATURE_FLAGS ${ARGN})
endmacro()

## Global GPU program definitions ####################################

# note: no longer offering default renderer names here,
# since custom names usually better stable IDs

add_computational_raytracer(RQ_CLOSEST "closest hit query")

add_computational_raytracer(GBUFFER "gbuffer")

add_gpu_program(PROCESS_TAA COMPUTE "temporal anti-aliasing")

## Global GPU program flags ####################################

if (COMPILING_FOR_DG2)
  list(APPEND GPU_COMPILE_DEFINITIONS "COMPILING_FOR_DG2")
endif()
if (ENABLE_REALTIME_RESOLVE)
  list(APPEND GPU_COMPILE_DEFINITIONS "ENABLE_REALTIME_RESOLVE")
endif()
if (ENABLE_DYNAMIC_MESHES)
  list(APPEND GPU_COMPILE_DEFINITIONS "ENABLE_DYNAMIC_MESHES")
endif()

# todo: do we want to support multi-config generators here?
if (CMAKE_BUILD_TYPE MATCHES "Debug|DEBUG")
  list(APPEND GPU_COMPILE_DEFINITIONS "_DEBUG")
endif ()

## For backend implementations of programs ####################

macro(add_gpu_sources _name)
  set(_multi_options COMPILE_DEFINITIONS PROGRAMS FEATURE_FLAGS PRECOMPILE_OPTIONS)
  cmake_parse_arguments(_gpu_sources "" "" "${_multi_options}" "${ARGN}")

  list(APPEND RPTR_GPU_PROGRAM_${_name}_DEFINES ${_gpu_sources_COMPILE_DEFINITIONS})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS ${_gpu_sources_PROGRAMS})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_FEATURE_FLAGS ${_gpu_sources_FEATURE_FLAGS})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_OPTIONS ${_gpu_sources_PRECOMPILE_OPTIONS})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_SOURCES ${_gpu_sources_UNPARSED_ARGUMENTS})

  if (NOT RPTR_GPU_PROGRAM_${_name}_CMDLINE)
    set(RPTR_GPU_PROGRAM_${_name}_CMDLINE "${GPU_PROGRAM_DEFAULT_CMDLINE}")
  endif ()
endmacro()

macro(apply_gpu_program_inheritance _name _inherited_name _source_inheritance_log_level)
  if (NOT RPTR_GPU_PROGRAM_${_name}_SOURCES AND NOT RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS
      OR "${_source_inheritance_log_level}" STREQUAL "")
    message(${_source_inheritance_log_level} "Program ${_name} inheriting sources from ${_inherited_name}")
    list(APPEND RPTR_GPU_PROGRAM_${_name}_SOURCES ${RPTR_GPU_PROGRAM_${_inherited_name}_SOURCES})
    list(APPEND RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS ${RPTR_GPU_PROGRAM_${_inherited_name}_SUBPROGRAMS})
  endif ()

  list(APPEND RPTR_GPU_PROGRAM_${_name}_DEFINES ${RPTR_GPU_PROGRAM_${_inherited_name}_DEFINES})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_FEATURE_FLAGS ${RPTR_GPU_PROGRAM_${_inherited_name}_FEATURE_FLAGS})
  list(APPEND RPTR_GPU_PROGRAM_${_name}_OPTIONS ${RPTR_GPU_PROGRAM_${_inherited_name}_OPTIONS})

  if (NOT RPTR_GPU_PROGRAM_${_name}_MODULE_NAME)
    set(RPTR_GPU_PROGRAM_${_name}_MODULE_NAME ${RPTR_GPU_PROGRAM_${_inherited_name}_MODULE_NAME})
  endif ()
  if (NOT RPTR_GPU_PROGRAM_${_name}_CMDLINE)
    set(RPTR_GPU_PROGRAM_${_name}_CMDLINE "${RPTR_GPU_PROGRAM_${_inherited_name}_CMDLINE}")
  endif ()
endmacro()

macro(resolve_gpu_programs)
  foreach(_name ${RPTR_GPU_PROGRAMS})
    foreach(_inherited_name ${RPTR_GPU_PROGRAM_${_name}_INHERITS})
      apply_gpu_program_inheritance(${_name} ${_inherited_name} VERBOSE)
    endforeach()
  endforeach()

  foreach(_name ${RPTR_GPU_PROGRAMS})
    set(_unfiltered_subprograms ${RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS})
    set(RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS)
    foreach(_subprogram_name ${_unfiltered_subprograms})
      if (_subprogram_name MATCHES "^\\*(.+)$")
        set(_subprogram_name ${CMAKE_MATCH_1})
        # create derived instance of referenced template subprogram
        list(LENGTH RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS _subprogram_count)
        set(_gpu_sources_new_subprogram ${_subprogram_name}_in_${_name}_SUBP${_subprogram_count})
        set(_gpu_sources_current_subprogram ${_subprogram_name})
        set(_gpu_sources_current_program_name ${_name})
        # template by inheriting referenced subprogram in new subprogram
        add_gpu_program(${_gpu_sources_new_subprogram} MODULE "${_subprogram_name} subprogram of ${_name}" INHERITS ${_subprogram_name} ${_name})
        apply_gpu_program_inheritance(${_gpu_sources_new_subprogram} ${_gpu_sources_current_subprogram} VERBOSE)
        apply_gpu_program_inheritance(${_gpu_sources_new_subprogram} ${_gpu_sources_current_program_name} FATAL_ERROR)
        set(_name ${_gpu_sources_current_program_name})
        set(_subprogram_name ${_gpu_sources_new_subprogram})
      endif ()
      list(APPEND RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS ${_subprogram_name})
    endforeach()
  endforeach()

  foreach(_name ${RPTR_GPU_PROGRAMS})
    # expand inline subprograms with (...) syntax
    set(_unfiltered_sources ${RPTR_GPU_PROGRAM_${_name}_SOURCES})
    set(RPTR_GPU_PROGRAM_${_name}_SOURCES)

    set(_gpu_sources_current_program_name ${_name})
    set(_gpu_sources_current_subprogram)
    foreach(_source_file ${_unfiltered_sources})
      if(_source_file STREQUAL "(")
        if(_gpu_sources_current_subprogram)
          message(FATAL_ERROR "Nested subprograms are not supported")
        endif()
        # begin new subprogram
        list(LENGTH RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS _subprogram_count)
        set(_gpu_sources_current_subprogram ${_name}_SUBP${_subprogram_count})

        add_gpu_program(${_gpu_sources_current_subprogram} MODULE "subprogram of ${_name}" INHERITS ${_name})
        # macro, reset state
        set(_name ${_gpu_sources_current_program_name})
      elseif(_source_file STREQUAL ")")
        # finalize currente subprogram
        if (_gpu_sources_current_subprogram)
          if (NOT RPTR_GPU_PROGRAM_${_gpu_sources_current_subprogram}_MODULE_NAME)
            list(GET RPTR_GPU_PROGRAM_${_gpu_sources_current_subprogram}_SOURCES -1 _last_source_file)
            get_filename_component(RPTR_GPU_PROGRAM_${_gpu_sources_current_subprogram}_MODULE_NAME "${_last_source_file}" NAME_WLE)
          endif ()
          apply_gpu_program_inheritance(${_gpu_sources_current_subprogram} ${_name} FATAL_ERROR)
          # macro, reset state
          set(_name ${_gpu_sources_current_program_name})
          # append to and resume main program
          list(APPEND RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS ${_gpu_sources_current_subprogram})
        endif ()
        set(_gpu_sources_current_subprogram)
      else()
        if(_gpu_sources_current_subprogram)
          # subprogram arg or source
          if(_source_file MATCHES "^([A-Za-z0-9_+-]+):$")
            set(RPTR_GPU_PROGRAM_${_gpu_sources_current_subprogram}_MODULE_NAME ${CMAKE_MATCH_1})
          elseif(_source_file MATCHES "^\\*(.+)$")
            apply_gpu_program_inheritance(${_gpu_sources_current_subprogram} ${CMAKE_MATCH_1} "")
            # macro, reset state
            set(_name ${_gpu_sources_current_program_name})
          elseif(_source_file MATCHES "^-D(.*)$")
            list(APPEND RPTR_GPU_PROGRAM_${_gpu_sources_current_subprogram}_DEFINES ${CMAKE_MATCH_1})
          else()
            list(APPEND RPTR_GPU_PROGRAM_${_gpu_sources_current_subprogram}_SOURCES ${_source_file})
          endif()
        else()
          # main program source
          list(APPEND RPTR_GPU_PROGRAM_${_name}_SOURCES ${_source_file})
        endif()
      endif()
    endforeach()
  endforeach()
endmacro()

macro(set_mono_gpu_programs)
  set(_mono_program_list ${ARGN})
  foreach(_name ${RPTR_GPU_PROGRAMS})
    list(FIND _mono_program_list ${_name} _in_mono_list_index)
    if (_in_mono_list_index EQUAL -1)
      set(RPTR_GPU_PROGRAM_${_name}_SOURCES )
      set(RPTR_GPU_PROGRAM_${_name}_SUBPROGRAMS )
    endif ()
  endforeach ()
endmacro()

## Configuring program metadata based on gpu_programs.h ####################

set(RPTR_GPU_PROGRAM_GENERATOR_DIR ${CMAKE_CURRENT_LIST_DIR})

# Compatible with (old) unprocessed Ninja depfile policy
if (CMAKE_GENERATOR MATCHES "Ninja")
  if (POLICY CMP0116)
    cmake_policy(SET CMP0116 OLD)
  endif ()
else ()
  if (CMAKE_VERSION VERSION_LESS "3.20")
    message(FATAL_ERROR "Shader change tracking only supported with Ninja generator!")
  endif ()
  cmake_policy(SET CMP0116 NEW)
endif ()

macro(get_current_gpu_build_base_dir out_variable)
  set(${out_variable} ${CMAKE_BINARY_DIR})
  if (POLICY CMP0116)
    cmake_policy(GET CMP0116 _CMP0116_value)
    if (_CMP0116_value STREQUAL NEW)
      set(${out_variable} ${CMAKE_CURRENT_BINARY_DIR})
    endif ()
  endif ()
endmacro()


function(generate_gpu_module_unit _source_file _program_name _program_dir)
  get_filename_component(GPU_EMBEDDED_OBJECT_NAME "${_source_file}" NAME_WLE)
  get_filename_component(GPU_EMBEDDED_OBJECT_TYPE "${_source_file}" LAST_EXT)
  string(REPLACE "." "" GPU_EMBEDDED_OBJECT_TYPE "${GPU_EMBEDDED_OBJECT_TYPE}")

  get_filename_component(GPU_EMBEDDED_OBJECT_ID "${_source_file}" DIRECTORY)
  set(GPU_EMBEDDED_OBJECT_ID "${GPU_EMBEDDED_OBJECT_NAME}_${GPU_EMBEDDED_OBJECT_TYPE}_${GPU_EMBEDDED_OBJECT_ID}_${_program_name}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" _ GPU_EMBEDDED_OBJECT_ID "${GPU_EMBEDDED_OBJECT_ID}")

  set(GPU_EMBEDDED_OBJECT_SRCPATH "${CMAKE_CURRENT_SOURCE_DIR}/${_source_file}")
  file(RELATIVE_PATH GPU_EMBEDDED_OBJECT_SRCPATH ${PROJECT_SOURCE_DIR} ${GPU_EMBEDDED_OBJECT_SRCPATH})

  set(GPU_EMBEDDED_OBJECT_FEATURE_FLAGS)
  foreach(_program_feature ${RPTR_GPU_PROGRAM_${_program_name}_FEATURE_FLAGS})
    string(APPEND GPU_EMBEDDED_OBJECT_FEATURE_FLAGS "GPU_PROGRAM_FEATURE_${_program_feature} | ")
  endforeach()

  # select correct source command line by unpacking nested lists, if any
  set(GPU_EMBEDDED_CMDLINE_ARGS "${RPTR_GPU_PROGRAM_${_program_name}_CMDLINE}")
  list(POP_FRONT GPU_EMBEDDED_CMDLINE_ARGS GPU_EMBEDDED_OBJECT_CLTOOL)
  if (GPU_EMBEDDED_OBJECT_CLTOOL STREQUAL ".*")
    list(POP_FRONT GPU_EMBEDDED_CMDLINE_ARGS GPU_EMBEDDED_OBJECT_CMDLINE)
    while (GPU_EMBEDDED_CMDLINE_ARGS)
      list(POP_FRONT GPU_EMBEDDED_CMDLINE_ARGS _pattern _cmd_line)
      if (_source_file MATCHES "${_pattern}")
        set(GPU_EMBEDDED_OBJECT_CMDLINE ${_cmd_line})
      endif ()
    endwhile ()
    string(REPLACE "`" ";" GPU_EMBEDDED_CMDLINE_ARGS "${GPU_EMBEDDED_OBJECT_CMDLINE}")
    list(POP_FRONT GPU_EMBEDDED_CMDLINE_ARGS GPU_EMBEDDED_OBJECT_CLTOOL)
  endif ()

  # run-time building
  string(REGEX REPLACE "\\$<[^:]*:(.*)>" "\\1" GPU_RUNTIME_OBJECT_CLTOOL ${GPU_EMBEDDED_OBJECT_CLTOOL})
  get_filename_component(GPU_RUNTIME_OBJECT_CLTOOL "${GPU_RUNTIME_OBJECT_CLTOOL}" NAME)

  set(GPU_EMBEDDED_OBJECT_CMDLINE)
  foreach(_cmd_arg ${GPU_RUNTIME_OBJECT_CLTOOL} ${GPU_EMBEDDED_CMDLINE_ARGS})
    string(APPEND GPU_EMBEDDED_OBJECT_CMDLINE "\"${_cmd_arg}\" ")
  endforeach()
  # todo: write cmdline to file and set path here instead
  string(REPLACE \\ \\\\ GPU_EMBEDDED_OBJECT_CMDPATH ${GPU_EMBEDDED_OBJECT_CMDLINE})
  string(REPLACE \" \\\" GPU_EMBEDDED_OBJECT_CMDPATH ${GPU_EMBEDDED_OBJECT_CMDPATH})

  set(GPU_EMBEDDED_OBJECT_CMDINPUT "\"${GPU_EMBEDDED_OBJECT_SRCPATH}\"")
  set(GPU_EMBEDDED_OBJECT_DEFINES)
  foreach(_program_define ${RPTR_GPU_PROGRAM_${_program_name}_DEFINES} ${GPU_COMPILE_DEFINITIONS})
    # todo: split into name, value pair where it contains '='
    string(APPEND GPU_EMBEDDED_OBJECT_DEFINES "{ \"${_program_define}\" }, ")

    list(APPEND GPU_EMBEDDED_OBJECT_CMDINPUT "-D${_program_define}")
  endforeach()
  # expand combinations of precompiled options
  # todo: deduplicate / merge multiple specifications of value groups?
  set(GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS " ") # note: cannot create seed list with empty string
  foreach(_render_option ${RPTR_GPU_PROGRAM_${_program_name}_OPTIONS})
    # expand multiple alternatives
    string(REPLACE "|" ";" _render_option_list "${_render_option}")
    # prepend option name to all alternatives
    list(POP_FRONT _render_option_list _render_option)
    if (_render_option MATCHES "^[A-Za-z_]+=")
      list(TRANSFORM _render_option_list PREPEND "${CMAKE_MATCH_0}")
    endif ()
    list(INSERT _render_option_list 0 "${_render_option}")
    # build cross product of all alternative variants
    set(_current_render_combinations "${GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS}")
    set(GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS)
    foreach(_render_combination ${_current_render_combinations})
      foreach(_render_option ${_render_option_list})
        if (_render_option MATCHES "[A-Za-z_]+=(|OFF|off|FALSE|false)$")
          list(APPEND GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS "${_render_combination}")
        elseif (_render_option MATCHES "([A-Za-z_]+)=(ON|on|TRUE|true)$")
          list(APPEND GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS "${_render_combination} -DRBO_${CMAKE_MATCH_1}")
        else ()
          list(APPEND GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS "${_render_combination} -DRBO_${_render_option}")
        endif ()
      endforeach()
    endforeach ()
  endforeach()

  file(RELATIVE_PATH GPU_EMBEDDED_OBJECT_CACHE_DIR ${PROJECT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/cache)
  file(TO_CMAKE_PATH GPU_EMBEDDED_OBJECT_CACHE_DIR ${GPU_EMBEDDED_OBJECT_CACHE_DIR})

  get_current_gpu_build_base_dir(CURRENT_BUILD_BASE_DIR)
  file(RELATIVE_PATH GPU_EMBEDDED_OBJECT_SOUCE_TO_BUILD_PATH ${PROJECT_SOURCE_DIR} ${CURRENT_BUILD_BASE_DIR})
  file(TO_CMAKE_PATH GPU_EMBEDDED_OBJECT_SOUCE_TO_BUILD_PATH ${GPU_EMBEDDED_OBJECT_SOUCE_TO_BUILD_PATH})

  set(GPU_EMBEDDED_ASTERISK *)
  set(CMAKE_GENERATE_GPU_MODULE_UNIT ON)
  set(GPU_EMBEDDED_MODULE_UNIT_CFILE ${_program_dir}/${GPU_EMBEDDED_OBJECT_ID}.c)
  configure_file(${RPTR_GPU_PROGRAM_GENERATOR_DIR}/gpu_programs.h ${GPU_EMBEDDED_MODULE_UNIT_CFILE})
  set_source_files_properties(${GPU_EMBEDDED_MODULE_UNIT_CFILE} PROPERTIES GPU_POGRAM_SOURCE "${_source_file}")

  set(GPU_EMBEDDED_MODULE_UNIT ${program_namespace_}${GPU_EMBEDDED_OBJECT_ID} PARENT_SCOPE)
  set(GPU_EMBEDDED_MODULE_UNIT_CFILE ${GPU_EMBEDDED_MODULE_UNIT_CFILE} PARENT_SCOPE)
  set(GPU_EMBEDDED_MODULE_UNIT_FEATURE_FLAGS ${GPU_EMBEDDED_OBJECT_FEATURE_FLAGS} PARENT_SCOPE)
  # todo: this is probably not the best way to extract, pass and use this info for modules
  set(GPU_EMBEDDED_MODULE_UNIT_NAME ${GPU_EMBEDDED_OBJECT_NAME} PARENT_SCOPE)
  set(GPU_EMBEDDED_OBJECT_TYPE ${GPU_EMBEDDED_OBJECT_TYPE} PARENT_SCOPE)

  # compile-time building
  set(GPU_EMBEDDED_OBJECT_RUNTIME_CMDLINE ${GPU_EMBEDDED_OBJECT_CMDLINE})

  set(GPU_EMBEDDED_OBJECT_FIXED_CMDINPUT ${GPU_EMBEDDED_OBJECT_CMDINPUT})
  set(GPU_EMBEDDED_OBJECT_BUILDCMD)
  foreach(_render_combination ${GPU_EMBEDDED_OBJECT_CMDOPTION_COMBINATIONS})
    string(REPLACE " -D" ";-D" _render_combination "${_render_combination}")
    string(REGEX REPLACE "^ *;| +$" "" _render_combination "${_render_combination}")
    list(SORT _render_combination)
    set(GPU_EMBEDDED_OBJECT_CMDINPUT ${GPU_EMBEDDED_OBJECT_FIXED_CMDINPUT} ${_render_combination})

    list(JOIN GPU_EMBEDDED_OBJECT_CMDINPUT " " GPU_EMBEDDED_OBJECT_HASH)
    string(SHA1 GPU_EMBEDDED_OBJECT_HASH "${GPU_EMBEDDED_OBJECT_RUNTIME_CMDLINE} ${GPU_EMBEDDED_OBJECT_HASH}")

    set(GPU_EMBEDDED_CACHE_FULLPATH ${PROJECT_BINARY_PRODUCTS_DIR}/${GPU_EMBEDDED_OBJECT_CACHE_DIR})
    set(GPU_EMBEDDED_OBJECT ${GPU_EMBEDDED_CACHE_FULLPATH}/${GPU_EMBEDDED_OBJECT_HASH}.spv)
    set(GPU_EMBEDDED_OBJECT_DEP ${GPU_EMBEDDED_CACHE_FULLPATH}/${GPU_EMBEDDED_OBJECT_HASH}.dep)

    file(RELATIVE_PATH GPU_PROJECT_SOURCE_IN_BUILD_TREE ${CURRENT_BUILD_BASE_DIR} ${PROJECT_SOURCE_DIR})
    file(RELATIVE_PATH GPU_EMBEDDED_OBJECT_IN_BUILD_TREE ${CURRENT_BUILD_BASE_DIR} ${GPU_EMBEDDED_OBJECT})

    list(POP_FRONT GPU_EMBEDDED_OBJECT_CMDINPUT)
    list(INSERT GPU_EMBEDDED_OBJECT_CMDINPUT 0 "\"${GPU_PROJECT_SOURCE_IN_BUILD_TREE}${GPU_EMBEDDED_OBJECT_SRCPATH}\"")

    set(GPU_EMBEDDED_OBJECT_CMDLINE)
    foreach(_cmd_arg ${GPU_EMBEDDED_OBJECT_CLTOOL} ${GPU_EMBEDDED_CMDLINE_ARGS})
      string(REGEX REPLACE "\\\${SOURCE_DIR}[/\\]?" ${GPU_PROJECT_SOURCE_IN_BUILD_TREE} _cmd_arg ${_cmd_arg})
      string(REPLACE "\${DEP_FILE}" ${GPU_EMBEDDED_OBJECT_DEP} _cmd_arg ${_cmd_arg})

      list(APPEND GPU_EMBEDDED_OBJECT_CMDLINE ${_cmd_arg})
    endforeach()

    # note: Ninja tracks output files relative to build tree root
    add_custom_command(OUTPUT ${GPU_EMBEDDED_OBJECT}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${GPU_EMBEDDED_CACHE_FULLPATH}
            COMMAND ${GPU_EMBEDDED_OBJECT_CMDLINE}
              ${GPU_EMBEDDED_OBJECT_CMDINPUT}
              -o ${GPU_EMBEDDED_OBJECT_IN_BUILD_TREE}
            DEPENDS ${_source_file}
            DEPFILE ${GPU_EMBEDDED_OBJECT_DEP}
            WORKING_DIRECTORY ${CURRENT_BUILD_BASE_DIR}
            COMMENT "Compiling ${_source_file} to ${GPU_EMBEDDED_OBJECT_IN_BUILD_TREE}")

    list(APPEND GPU_EMBEDDED_OBJECT_BUILDCMD ${GPU_EMBEDDED_OBJECT})
  endforeach ()
  set(GPU_EMBEDDED_OBJECT_BUILDCMD ${GPU_EMBEDDED_OBJECT_BUILDCMD} PARENT_SCOPE)
endfunction()


function(generate_gpu_module _program_name _program_dir _sources)
  set(GPU_EMBEDDED_MODULE_OBJECTS)
  set(GPU_EMBEDDED_MODULE_OBJECT_POINTERS)
  set(GPU_EMBEDDED_MODULE_FEATURE_FLAGS)
  set(GPU_EMBEDDED_MODULE_CFILES)
  set(GPU_EMBEDDED_MODULE_BUILDCMDS)
  foreach(_source_file ${_sources})
    generate_gpu_module_unit(${_source_file} ${_program_name} ${_program_dir})
    string(APPEND GPU_EMBEDDED_MODULE_OBJECTS "${GPU_EMBEDDED_MODULE_UNIT}, ")
    string(APPEND GPU_EMBEDDED_MODULE_OBJECT_POINTERS "&${GPU_EMBEDDED_MODULE_UNIT}, ")
    string(APPEND GPU_EMBEDDED_MODULE_FEATURE_FLAGS ${GPU_EMBEDDED_MODULE_UNIT_FEATURE_FLAGS})
    list(APPEND GPU_EMBEDDED_MODULE_CFILES ${GPU_EMBEDDED_MODULE_UNIT_CFILE})
    list(APPEND GPU_EMBEDDED_MODULE_BUILDCMDS ${GPU_EMBEDDED_OBJECT_BUILDCMD})
  endforeach()

  if (RPTR_GPU_PROGRAM_${_program_name}_MODULE_NAME)
    set(GPU_EMBEDDED_MODULE_NAME ${RPTR_GPU_PROGRAM_${_program_name}_MODULE_NAME})
  else()
    set(GPU_EMBEDDED_MODULE_NAME ${GPU_EMBEDDED_MODULE_UNIT_NAME})
  endif()
  set(GPU_EMBEDDED_MODULE_ID module_${GPU_EMBEDDED_MODULE_UNIT})
  set(GPU_EMBEDDED_MODULE_TYPE ${GPU_EMBEDDED_OBJECT_TYPE})
  set(GPU_EMBEDDED_ASTERISK *)
  set(CMAKE_GENERATE_GPU_MODULE ON)

  set(GPU_EMBEDDED_MODULE_CFILE ${_program_dir}/${GPU_EMBEDDED_MODULE_ID}.c)
  configure_file(${RPTR_GPU_PROGRAM_GENERATOR_DIR}/gpu_programs.h ${GPU_EMBEDDED_MODULE_CFILE})
  list(APPEND GPU_EMBEDDED_MODULE_CFILES ${GPU_EMBEDDED_MODULE_CFILE})

  set(GPU_EMBEDDED_MODULE ${program_namespace_}${GPU_EMBEDDED_MODULE_ID} PARENT_SCOPE)
  set(GPU_EMBEDDED_MODULE_CFILES "${GPU_EMBEDDED_MODULE_CFILES}" PARENT_SCOPE)
  set(GPU_EMBEDDED_MODULE_FEATURE_FLAGS "${GPU_EMBEDDED_MODULE_FEATURE_FLAGS}" PARENT_SCOPE)
  set(GPU_EMBEDDED_MODULE_BUILDCMDS "${GPU_EMBEDDED_MODULE_BUILDCMDS}" PARENT_SCOPE)
endfunction()


function(generate_gpu_program _program_name _program_dir)
  set(GPU_EMBEDDED_PROGRAM_ID ${_program_name})
  set(GPU_EMBEDDED_PROGRAM_NAME ${RPTR_GPU_PROGRAM_${_program_name}_LABEL})
  set(GPU_EMBEDDED_PROGRAM_TYPE ${RPTR_GPU_PROGRAM_${_program_name}_TYPE})

  set(GPU_EMBEDDED_PROGRAM_VARIABLE ${program_namespace_}${GPU_EMBEDDED_PROGRAM_ID})
  set(GPU_EMBEDDED_PROGRAM ${GPU_EMBEDDED_PROGRAM_VARIABLE} PARENT_SCOPE)
  set(GPU_EMBEDDED_PROGRAM_CFILES PARENT_SCOPE)
  # do not generate and return c files again if program already embedded
  get_property(_preexisiting_id_variable DIRECTORY PROPERTY RPTR_GPU_PROGRAM_${_program_name}_VARIABLE)
  if(_preexisiting_id_variable)
    return()
  endif()

  set(MY_PROGRAM_DIR ${_program_dir}/${_program_name})
  set(MY_PROGRAM_CFILES)
  set(MY_PROGRAM_MODULES)
  set(MY_PROGRAM_MODULE_POINTERS)
  set(MY_PROGRAM_FEATURE_FLAGS)
  set(MY_PROGRAM_BUILD_CMDS)

  # turn program into one module per source file unless type is combined "MODULE"
  if(RPTR_GPU_PROGRAM_${_program_name}_TYPE STREQUAL "MODULE")
    set(_module_source_lists COMBINED-MODULE-SOURCES)
  else()
    set(_module_source_lists ${RPTR_GPU_PROGRAM_${_program_name}_SOURCES})
  endif()
  foreach(_source_list ${_module_source_lists})
    if (_source_list STREQUAL COMBINED-MODULE-SOURCES)
      set(_source_list ${RPTR_GPU_PROGRAM_${_program_name}_SOURCES})
    endif ()
    generate_gpu_module(${_program_name} ${MY_PROGRAM_DIR} "${_source_list}")
    string(APPEND MY_PROGRAM_MODULES "${GPU_EMBEDDED_MODULE}, ")
    string(APPEND MY_PROGRAM_MODULE_POINTERS "&${GPU_EMBEDDED_MODULE}, ")
    string(APPEND MY_PROGRAM_FEATURE_FLAGS ${GPU_EMBEDDED_MODULE_FEATURE_FLAGS})
    list(APPEND MY_PROGRAM_CFILES ${GPU_EMBEDDED_MODULE_CFILES})
    list(APPEND MY_PROGRAM_BUILD_CMDS ${GPU_EMBEDDED_MODULE_BUILDCMDS})
  endforeach()

  # add subprograms
  foreach(_subprogram_name ${RPTR_GPU_PROGRAM_${_program_name}_SUBPROGRAMS})
    generate_gpu_program(${_subprogram_name} ${MY_PROGRAM_DIR})
    string(APPEND MY_PROGRAM_MODULES "${GPU_EMBEDDED_PROGRAM_MODULES}")
    string(APPEND MY_PROGRAM_MODULE_POINTERS "${GPU_EMBEDDED_PROGRAM_MODULE_POINTERS}")
    string(APPEND MY_PROGRAM_FEATURE_FLAGS ${GPU_EMBEDDED_PROGRAM_FEATURE_FLAGS})
    list(APPEND MY_PROGRAM_CFILES ${GPU_EMBEDDED_PROGRAM_CFILES})
    list(APPEND MY_PROGRAM_BUILD_CMDS ${GPU_EMBEDDED_PROGRAM_BUILD_CMDS})
  endforeach()

  if(NOT MY_PROGRAM_CFILES)
    return()
  endif()

  set(GPU_EMBEDDED_ASTERISK *)
  set(CMAKE_GENERATE_GPU_PROGRAM ON)
  set(GPU_EMBEDDED_PROGRAM_MODULES "${MY_PROGRAM_MODULES}")
  set(GPU_EMBEDDED_PROGRAM_MODULE_POINTERS "${MY_PROGRAM_MODULE_POINTERS}")
  set(GPU_EMBEDDED_PROGRAM_FEATURE_FLAGS "${MY_PROGRAM_FEATURE_FLAGS}")

  set(GPU_EMBEDDED_PROGRAM_CFILE ${MY_PROGRAM_DIR}/${_program_name}.c)
  configure_file(${RPTR_GPU_PROGRAM_GENERATOR_DIR}/gpu_programs.h ${GPU_EMBEDDED_PROGRAM_CFILE})
  list(APPEND MY_PROGRAM_CFILES ${GPU_EMBEDDED_PROGRAM_CFILE})

  set(GPU_EMBEDDED_PROGRAM_MODULES "${MY_PROGRAM_MODULES}" PARENT_SCOPE)
  set(GPU_EMBEDDED_PROGRAM_MODULE_POINTERS "${MY_PROGRAM_MODULE_POINTERS}" PARENT_SCOPE)
  set(GPU_EMBEDDED_PROGRAM_CFILES "${MY_PROGRAM_CFILES}" PARENT_SCOPE)
  set(GPU_EMBEDDED_PROGRAM_FEATURE_FLAGS "${MY_PROGRAM_FEATURE_FLAGS}" PARENT_SCOPE)
  set(GPU_EMBEDDED_PROGRAM_BUILD_CMDS "${MY_PROGRAM_BUILD_CMDS}" PARENT_SCOPE)

  set_property(DIRECTORY PROPERTY RPTR_GPU_PROGRAM_${_program_name}_VARIABLE ${GPU_EMBEDDED_PROGRAM_VARIABLE})
endfunction()


function(generate_gpu_programs _gpu_programs_prefix)
  set(_program_dir gpu_programs)

  # namespace set here for all generators
  set(program_namespace_ ${_gpu_programs_prefix}_program_)

  set(GPU_EMBEDDED_CFILES)
  set(GPU_EMBEDDED_PROGRAMS)
  set(GPU_EMBEDDED_PROGRAM_POINTERS)
  set(GPU_EMBEDDED_BUILD_CMDS)
  foreach(_program_name ${RPTR_GPU_PROGRAMS})
    if(NOT RPTR_GPU_PROGRAM_${_program_name}_TYPE STREQUAL "MODULE")
      generate_gpu_program(${_program_name} ${_program_dir})
      # only collect programs with attached modules/sources
      if (GPU_EMBEDDED_PROGRAM_CFILES)
        string(APPEND GPU_EMBEDDED_PROGRAMS "${GPU_EMBEDDED_PROGRAM}, ")
        string(APPEND GPU_EMBEDDED_PROGRAM_POINTERS "&${GPU_EMBEDDED_PROGRAM}, ")
        list(APPEND GPU_EMBEDDED_CFILES ${GPU_EMBEDDED_PROGRAM_CFILES})
        list(APPEND GPU_EMBEDDED_BUILD_CMDS ${GPU_EMBEDDED_PROGRAM_BUILD_CMDS})
      endif ()
    endif()
  endforeach()

  set(GPU_EMBEDDED_INTEGRATOR_POINTERS)
  foreach(_program_name ${RPTR_INTEGRATORS})
    get_property(_preexisiting_id_variable DIRECTORY PROPERTY RPTR_GPU_PROGRAM_${_program_name}_VARIABLE)
    if (_preexisiting_id_variable)
      string(APPEND GPU_EMBEDDED_INTEGRATOR_POINTERS "&${_preexisiting_id_variable}, ")
    endif ()
  endforeach()

  set(GPU_EMBEDDED_COMPUTATIONAL_RAYTRACERS)
  foreach(_program_name ${RPTR_COMPUTATIONAL_RAYTRACERS})
    get_property(_preexisiting_id_variable DIRECTORY PROPERTY RPTR_GPU_PROGRAM_${_program_name}_VARIABLE)
    if (_preexisiting_id_variable)
      string(APPEND GPU_EMBEDDED_COMPUTATIONAL_RAYTRACER_POINTERS "&${_preexisiting_id_variable}, ")
    endif ()
  endforeach()

  set(GPU_EMBEDDED_ASTERISK *)
  set(CMAKE_GENERATE_GPU_PROGRAM_LIST ON)
  set(GPU_EMBEDDED_PROGRAMS_PREFIX ${_gpu_programs_prefix})

  set(GPU_EMBEDDED_PROGRAMS_CFILE ${_program_dir}/gpu_programs.c)
  configure_file(${RPTR_GPU_PROGRAM_GENERATOR_DIR}/gpu_programs.h ${GPU_EMBEDDED_PROGRAMS_CFILE})
  #set_source_files_properties(${GPU_EMBEDDED_PROGRAMS_CFILE} )
  list(APPEND GPU_EMBEDDED_CFILES ${GPU_EMBEDDED_PROGRAMS_CFILE})

  foreach (_desc_file ${GPU_EMBEDDED_CFILES})
    set(_source_file)
    get_source_file_property(_source_file ${_desc_file} GPU_POGRAM_SOURCE)
    if (_source_file)
      list(APPEND GPU_POGRAM_SOURCES ${_source_file})
    endif ()
  endforeach ()
  list(REMOVE_DUPLICATES GPU_POGRAM_SOURCES)

  set(GPU_EMBEDDED_CFILES "${GPU_EMBEDDED_CFILES}" PARENT_SCOPE)
  set(GPU_EMBEDDED_BUILD_CMDS "${GPU_EMBEDDED_BUILD_CMDS}" PARENT_SCOPE)
  set(GPU_POGRAM_SOURCES "${GPU_POGRAM_SOURCES}" PARENT_SCOPE)
endfunction()
