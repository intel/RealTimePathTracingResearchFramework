#!/bin/sh
# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT


SCRIPT_FILE=$(readlink -f ${0})
SCRIPT_DIR=$(dirname ${SCRIPT_FILE})

if ! ( command -v blender &> /dev/null )
then
  echo "Cannot find blender"
  exit 1
fi

# Optional argument: module source dir.
if [ $# -gt 0 ]
then
  SCRIPT_DIR="$1"
fi

MODULE_ARCHIVE="${SCRIPT_DIR}/blender_vkr.zip"

if [ ! -f "${MODULE_ARCHIVE}" ]
then
  echo "Cannot find module archive ${MODULE_ARCHIVE}"
  exit 2
fi

INSTALL_SCRIPT='
import bpy
import os
import sys

module_name = "blender_vkr"
cwd = os.getcwd()
module_file = os.path.join(cwd, module_name + ".zip")

if not os.path.isfile(module_file):
  print(f"Cannot find module archive {module_file}")
  sys.exit(1)

bpy.ops.preferences.addon_install(overwrite=True, filepath=module_file)
bpy.ops.preferences.addon_enable(module=module_name)
# Required so that blender does not forget we enabled.
bpy.ops.wm.save_userpref()
'

cd "${SCRIPT_DIR}"
blender --background --python-expr "${INSTALL_SCRIPT}"
