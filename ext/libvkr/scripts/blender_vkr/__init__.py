# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

import bpy

is_command_line_test_run = __name__ == "__main__"
if is_command_line_test_run:
    import sys
    import os
    print("Note: Running external blender_vkr addins from command line")
    sys.path.insert(0, os.getcwd())
    sys.path.insert(0, os.path.abspath(os.path.dirname(__file__)))
    print(sys.path)

# NOTE: If you use pyvkr functionality, please guard that by checking
#       have_pyvkr.
try:
    if not is_command_line_test_run:
        from . import pyvkr
    else:
        import pyvkr
    have_pyvkr = True
except Exception as e:
    print(f"Failed to load pyvkr. This probably means that your blender version is quite old; try a more recent one!")
    print(e)
    have_pyvkr = False

bl_info = {
    "name": ".vks/.vkt [blender_vkr]",
    "blender": (3,1,0),
    "category": "Export",
}

try:
    if not is_command_line_test_run:
        from . import operator_file_export_vkrs as vks
    else:
        import operator_file_export_vkrs as vks
    vks.pyvkr = pyvkr
    vks.have_pyvkr = have_pyvkr
except Exception as e:
    print(e)
try:
    if not is_command_line_test_run:
        from . import operator_file_export_camera_path as cp
    else:
        import operator_file_export_camera_path as cp
except Exception as e:
    print(e)
try:
    if not is_command_line_test_run:
        from . import operator_file_export_pbr_textures as vkt
    else:
        import operator_file_export_pbr_textures as vkt
    vkt.pyvkr = pyvkr
    vkt.have_pyvkr = have_pyvkr
except Exception as e:
    print(e)

def register():
    try:
        vks.register()
    except Exception as e:
        print(e)
    try:
        cp.register()
    except Exception as e:
        print(e)
    try:
        if have_pyvkr:
            vkt.register()
    except Exception as e:
        print(e)

def unregister():
    try:
        vks.unregister()
    except Exception as e:
        print(e)
    try:
        cp.unregister()
    except Exception as e:
        print(e)
    try:
        if have_pyvkr:
            vkt.unregister()
    except Exception as e:
        print(e)

# Test call, installed blender addin does not execute this.
if is_command_line_test_run:
    register()
#    bpy.ops.export.vulkan_renderer_scene('EXEC_DEFAULT',
#        filepath="export-test-run2.vks")
#    bpy.ops.rptr.export_camera_path('INVOKE_DEFAULT')
