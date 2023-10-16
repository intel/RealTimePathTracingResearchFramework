// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef GLSL_LANGUAGE_ADAPTER
#define GLSL_LANGUAGE_ADAPTER

// types
#define uint32_t uint
#define int32_t int

// qualifiers
#define GLSL_out(type) out type
#define GLSL_inout(type) inout type
#define GLSL_in(type) in const type
#define GLSL_to_out_ptr(lvalue) lvalue

#define GLSL_construct
#define GLSL_unused(x) x
#define GLM(type) type
#define GLCPP_DEFAULT(x)

// language-wide configuration
#extension GL_EXT_buffer_reference : enable

#define GLSL_SPLIT_INT64
#define GLSL_UINT64 uvec2

#extension GL_KHR_shader_subgroup_ballot : enable
#extension GL_KHR_shader_subgroup_vote : enable
#extension GL_KHR_shader_subgroup_arithmetic : enable

#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_nonuniform_qualifier : enable

#extension GL_EXT_control_flow_attributes : require

#define UNROLL_FOR [[unroll]] for
#define DYNAMIC_FOR [[dont_unroll]] for

#define inline

inline vec2 fma2(vec2 a, vec2 b, vec2 c) {
    return fma(a, b, c);
}
inline vec3 fma3(vec3 a, vec3 b, vec3 c) {
    return fma(a, b, c);
}

#endif
