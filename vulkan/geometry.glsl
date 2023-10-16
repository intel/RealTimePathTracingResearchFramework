// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

layout(buffer_reference, buffer_reference_align=8, scalar) buffer VertexBuffer {
#ifdef QUANTIZED_POSITIONS
    GLSL_UINT64 v[];
#else
    vec3 v[];
#endif
};

layout(buffer_reference, buffer_reference_align=4, scalar) buffer IndexBuffer {
    uvec3 i[];
};

layout(buffer_reference, buffer_reference_align=8, scalar) buffer NormalBuffer {
#ifdef QUANTIZED_NORMALS_AND_UVS
    GLSL_UINT64 n_uvs[];
#else
    vec3 n[];
#endif
};

#ifndef QUANTIZED_NORMALS_AND_UVS
layout(buffer_reference, buffer_reference_align=8, scalar) buffer UVBuffer {
    vec2 uv[];
};
#endif

layout(buffer_reference, buffer_reference_align=4, scalar) buffer MaterialBuffer {
    uint32_t id_4pack[];
};

#ifdef ENABLE_REALTIME_RESOLVE
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#endif
layout(buffer_reference, buffer_reference_align=4, scalar) buffer MotionVectorBuffer {
#ifdef ENABLE_REALTIME_RESOLVE
    float16_t m_components[]; // triplets of halfs
#else
    vec4 m_void[];
#endif
};

#define VERTEX_BUFFER_TYPE VertexBuffer

#ifdef QUANTIZED_POSITIONS
#define QUANTIZED_VERTEX_BUFFER_TYPE VertexBuffer
// dynamic geometry always requires full-precision vertex buffer
layout(buffer_reference, buffer_reference_align=4, scalar) buffer DynamicVertexBuffer {
    vec3 v[];
};
#define PLAIN_VERTEX_BUFFER_TYPE DynamicVertexBuffer
#else
#define PLAIN_VERTEX_BUFFER_TYPE VertexBuffer
#endif

#define NORMAL_BUFFER_TYPE NormalBuffer
#define UV_BUFFER_TYPE UVBuffer

#ifdef QUANTIZED_NORMALS_AND_UVS
#define QUANTIZED_NORMAL_UV_BUFFER_TYPE NormalBuffer
#undef UV_BUFFER_TYPE
#define UV_BUFFER_TYPE uvec2
#else
#define PLAIN_NORMAL_BUFFER_TYPE NormalBuffer
#define PLAIN_UV_BUFFER_TYPE UVBuffer
#endif

#define MATERIAL_ID_BUFFER_TYPE MaterialBuffer
#define INDEX_BUFFER_TYPE IndexBuffer
#define MOTION_VECTOR_BUFFER_TYPE MotionVectorBuffer

#include "rt/geometry.h.glsl"

float geometry_scale_to_tmin(vec3 orig, float geometry_scale) {
    return (length(orig) + geometry_scale) * RAY_EPSILON;
}

float geometry_scale_from_tmin(vec3 orig, float tmin) {
    return max(tmin / RAY_EPSILON - length(orig), 0.0f);
}
