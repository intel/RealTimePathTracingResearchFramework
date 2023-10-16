// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MATERIAL_DECODE_HEADER
#define MATERIAL_DECODE_HEADER

#include "../bsdfs/texture_channel_mask.h"
#include "../bsdfs/hit_point.glsl"

// #define MATERIAL_PARAMS
// #define MATERIAL_TYPE

#ifndef MATERIAL_PARAMS
    #include "../bsdfs/base_material.h.glsl"
    #define MATERIAL_PARAMS BaseMaterial
#endif

struct EmitterInteraction {
    vec3 radiance;
};
// backwards compatibility
#ifndef EmitterParams
    #define EmitterParams EmitterInteraction
#endif

inline vec4 textured_color_param(const vec4 x, GLSL_in(HitPoint) hit);
inline float textured_scalar_param(const float x, GLSL_in(HitPoint) hit);

// accessors for potentially unrolled standard textures
#if defined(UNROLL_STANDARD_TEXTURES) && !defined(MATERIAL_DECODE_CUSTOM_TEXTURES)

inline vec4 textured_standard_param(uint32_t material_id, GLSL_in(HitPoint) hit, int slot);
inline float textured_channel_dynamic(const vec4 x, uint32_t channel);

#define textured_scalar_standard_param(material_id, value, hit, slot, channel) \
    textured_channel_dynamic(textured_standard_param(material_id, hit, slot), channel)
#define textured_color_standard_param(material_id, value, hit, slot) \
    textured_standard_param(material_id, hit, slot)

#else

#ifndef textured_scalar_standard_param
#define textured_scalar_standard_param(material_id, value, hit, slot, channel) \
    textured_scalar_param(value, hit)
#endif
#ifndef textured_color_standard_param
#define textured_color_standard_param(material_id, value, hit, slot) \
    textured_color_param(value, hit)
#endif

#endif

#ifndef ENABLE_MATERIAL_DECODE
    #define ENABLE_MATERIAL_DECODE // enable functionality in subsequent material headers
#endif

#endif
