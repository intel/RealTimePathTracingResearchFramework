// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require

#include "language.glsl"
#include "../gpu_params.glsl"

layout(binding = VIEW_PARAMS_BIND_POINT, set = 0, std140) uniform VBuf {
    LOCAL_CONSTANT_PARAMETERS
};
layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform SBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

#include "../geometry.glsl"

layout(binding = INSTANCES_BIND_POINT, set = 0, std430) buffer GeometryParamsBuffer {
    InstancedGeometry instances[];
};

layout(push_constant) uniform PushConstants {
    RenderMeshParams geometry;
};

#ifdef QUANTIZED_POSITIONS
    layout(location = 0) in uvec2 vertex_position;
#else
    layout(location = 0) in vec3 vertex_position;
#endif
#ifdef QUANTIZED_NORMALS_AND_UVS
    layout(location = 1) in uint vertex_normal;
    layout(location = 2) in uint vertex_uv;
#else
    layout(location = 1) in vec3 vertex_normal;
    layout(location = 2) in vec2 vertex_uv;
#endif

layout(location = 4) in uint32_t instance_geo_idx;

#if defined(QUANTIZED_VERTEX_BUFFER_TYPE) || defined(QUANTIZED_NORMAL_UV_BUFFER_TYPE)
    #include "../../librender/dequantize.glsl"
#endif

layout(location = 0) out vec2 pixel_uv;
layout(location = 1) out vec3 pixel_normal;
layout(location = 2) out vec3 pixel_pos;

void main() {
#ifdef QUANTIZED_POSITIONS
    vec3 p = DEQUANTIZE_POSITION(vertex_position, geometry.quantized_scaling, geometry.quantized_offset);
#else
    vec3 p = vertex_position;
#endif
#ifdef QUANTIZED_NORMALS_AND_UVS
    vec3 n = dequantize_normal(vertex_normal);
#else
    vec3 n = vertex_normal;
#endif
#ifdef QUANTIZED_NORMALS_AND_UVS
    vec2 uv = dequantize_uv(vertex_uv);
#else
    vec2 uv = vertex_uv;
#endif

#ifdef OBJECT_SPACE_VERTEX_TRANSFORMATION_EXTENSION
    OBJECT_SPACE_VERTEX_TRANSFORMATION_EXTENSION(p, n, uv);
#endif

    #define instance instances[instance_geo_idx]
    mat4 instance_to_world = instance.instance_to_world;
    mat4 mvp = view_params.VP * instance_to_world;

    pixel_normal = normalize(transpose(mat3(instance.world_to_instance)) * n);
    pixel_uv = uv;
    pixel_pos = vec3(instance_to_world * vec4(p, 1.0f));
    gl_Position = mvp * vec4(p, 1.0f);
    gl_Position.xy += view_params.screen_jitter * gl_Position.w;
}
