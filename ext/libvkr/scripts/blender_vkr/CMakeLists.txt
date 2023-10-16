# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

set(BLENDER_ADDON blender_vkr)
set(BLENDER_ADDON_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/${BLENDER_ADDON})
set(BLENDER_ADDON_ARCHIVE ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/${BLENDER_ADDON}.zip)

# This has to be first to set up the archive target.
add_custom_command(OUTPUT ${BLENDER_ADDON_ARCHIVE}
  COMMENT "Building ${BLENDER_ADDON_ARCHIVE} ..."
  COMMAND ${CMAKE_COMMAND} -E remove_directory ${BLENDER_ADDON_OUTPUT_DIR} # backwards-compatible for `rm -rf`
  COMMAND ${CMAKE_COMMAND} -E make_directory ${BLENDER_ADDON_OUTPUT_DIR}
)

# Takes a list of files in scripts/blender_vkr as optional arguments.
function(blender_addon_sources)
  foreach(F IN LISTS ARGN)
    set(IN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/${F})
    set(OUT_FILE ${BLENDER_ADDON_OUTPUT_DIR}/${F})
    add_custom_command(
      APPEND OUTPUT ${BLENDER_ADDON_ARCHIVE}
      DEPENDS ${IN_FILE}
      COMMAND ${CMAKE_COMMAND} -E copy ${IN_FILE} ${OUT_FILE}
    )
  endforeach()
endfunction()

blender_addon_sources(
  __init__.py
  operator_file_export_vkrs.py
  operator_file_export_camera_path.py
  operator_file_export_pbr_textures.py
)

if (TARGET pyvkr)
  add_custom_command(
    APPEND OUTPUT ${BLENDER_ADDON_ARCHIVE}
    DEPENDS pyvkr
    COMMAND ${CMAKE_COMMAND} -E
      copy $<TARGET_FILE:pyvkr> ${BLENDER_ADDON_OUTPUT_DIR}/$<TARGET_FILE_NAME:pyvkr>
  )
else ()
  message("Blender tools built without tool support library, incomplete functionality!")
  add_custom_command(
    APPEND OUTPUT ${BLENDER_ADDON_ARCHIVE}
    COMMAND ${CMAKE_COMMAND} -E
      touch ${BLENDER_ADDON_OUTPUT_DIR}/warn_built_with_incomplete_tool_support
  )
endif ()

# Finalize blender plugin archive
add_custom_command(
  APPEND OUTPUT ${BLENDER_ADDON_ARCHIVE}
  COMMAND ${CMAKE_COMMAND} -E tar c ${BLENDER_ADDON_ARCHIVE} --format=zip
    ${BLENDER_ADDON_OUTPUT_DIR}
)

set(BLENDER_ADDON_INSTALL_SCRIPT_BPY ${CMAKE_BINARY_DIR}/install_blender_addon.bpy)
add_custom_command(
  OUTPUT ${BLENDER_ADDON_INSTALL_SCRIPT_BPY}
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../install_blender_addon.bpy
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../install_blender_addon.bpy ${BLENDER_ADDON_INSTALL_SCRIPT_BPY}
)
# Add optional targets for automatic install
if(UNIX)
  set(BLENDER_ADDON_INSTALL_SCRIPT ${CMAKE_BINARY_DIR}/install_blender_addon.sh)
  add_custom_command(
    OUTPUT ${BLENDER_ADDON_INSTALL_SCRIPT}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../install_blender_addon.sh
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/../install_blender_addon.sh ${BLENDER_ADDON_INSTALL_SCRIPT}
  )
  add_custom_target(install_${BLENDER_ADDON}
    DEPENDS ${BLENDER_ADDON_INSTALL_SCRIPT}
    COMMAND ${BLENDER_ADDON_INSTALL_SCRIPT})
endif()

add_custom_target(${BLENDER_ADDON} ALL
  DEPENDS
    ${BLENDER_ADDON_ARCHIVE}
    ${BLENDER_ADDON_INSTALL_SCRIPT_BPY}
    ${BLENDER_ADDON_INSTALL_SCRIPT}
)
if (TARGET install_${BLENDER_ADDON})
  add_dependencies(install_${BLENDER_ADDON} ${BLENDER_ADDON})
endif ()
