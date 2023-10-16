# Copyright 2023 Intel Corporation.
# SPDX-License-Identifier: MIT

import bpy
import mathutils

def write_camera_matrix(stream, m):
    pw = mathutils.Vector((0, 0, 0))
    uw = mathutils.Vector((0, 1, 0))
    dw = mathutils.Vector((0, 0, -1))
    p = m @ pw
    u = m.to_quaternion() @ uw
    d = m.to_quaternion() @ dw
    stream.write("[Application][Scene]\n")
    stream.write("[.][Camera]\n")
    # Note that we rotate into the Vulkan coordinate frame here, this is
    #  Rx(-pi/2) * Rz(pi).
    stream.write(f"position= {-p[0]} {p[2]} {p[1]}\n")
    stream.write(f"direction= {-d[0]} {d[2]} {d[1]}\n")
    stream.write(f"up= {-u[0]} {u[2]} {u[1]}\n")
    stream.write("..\n")

def write_camera_path(context, filepath, frame_range, intent, camera):
    s = bpy.context.scene
    time_delta = ""
    if intent == "REAL_TIME":
        seconds_per_frame = s.render.fps_base / s.render.fps
        time_delta = f"+{seconds_per_frame}"
    old_frame = s.frame_current
    with open(filepath, 'w', encoding='utf-8') as f:
        for frame in range(frame_range[0], frame_range[1]+1):
            s.frame_set(frame)
            write_camera_matrix(f, camera.matrix_world)
            f.write(f"[;][{time_delta}]\n") # End of frame marker.

    s.frame_set(old_frame)
    f.close()

    return {'FINISHED'}


# ExportHelper is a helper class, defines filename and
# invoke() function which calls the file selector.
from bpy_extras.io_utils import ExportHelper
from bpy.props import StringProperty, BoolProperty, EnumProperty, IntVectorProperty
from bpy.types import Operator


class ExportCameraPath(Operator, ExportHelper):
    """Export camera path to Real-time Path Tracing Research Framework config files"""
    bl_idname = "rptr.export_camera_path"  # important since its how bpy.ops.import_test.some_data is constructed
    bl_label = "Export Camera Path"

    # ExportHelper mixin class uses this
    filename_ext = ".ini"

    filter_glob: StringProperty(
        default="*.ini",
        options={'HIDDEN'},
        maxlen=255,  # Max internal buffer length, longer would be clamped.
    )

    frame_range: IntVectorProperty(
        name="Frame Range",
        description="The frame range to export",
        size=2
    )

    intent: EnumProperty(
        items=(
            ('REAL_TIME', "Real-time", "The camera path will be exported with timestamps. This is most suitable for real-time rendering."),
            ('PROFILING', "Profiling", "The camera path will be exported without timestamps. This is most suitable for FPS-locked profiling."),
        ),
        name="Intent",
        description="Indicate whether the path is intended for real-time rendering or (fps-locked) profiling.",
        default="REAL_TIME"
    )

    def invoke(self, context, _event):
        self.frame_range = (bpy.context.scene.frame_start, bpy.context.scene.frame_end)
        self.cameras = [ c for c in bpy.context.selected_objects if c.type == 'CAMERA' ]
        if self.cameras:
            self.filepath = f"{self.cameras[0].name}.ini"
            self.report({'INFO'}, f"exporting camera path for {self.cameras[0].name}")
            return super().invoke(context, _event)
        self.report({'ERROR'}, "No camera selected.")      
        return {'CANCELLED'}

    def execute(self, context):
        cam = self.cameras[0]
        return write_camera_path(context, self.filepath, self.frame_range,
            self.intent, self.cameras[0])


# Only needed if you want to add into a dynamic menu
def menu_func_export(self, context):
    self.layout.operator(ExportCameraPath.bl_idname, text="Export camera path to the Real-time Path Tracing Research Framework")

def register():
    bpy.utils.register_class(ExportCameraPath)
    bpy.types.TOPBAR_MT_file_export.append(menu_func_export)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_func_export)
    bpy.utils.unregister_class(ExportCameraPath)
