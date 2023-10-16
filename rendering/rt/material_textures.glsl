// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef MATERIAL_TEXTURE_DECODE_HEADER
#define MATERIAL_TEXTURE_DECODE_HEADER

#include "../bsdfs/texture_channel_mask.h"
#include "../bsdfs/hit_point.glsl"

// #define SCENE_GET_TEXTURE(index)
// #define SCENE_GET_STANDARD_TEXTURE(index)
// #define MATERIAL_PARAMS
// #define MATERIAL_TYPE

// default GLSL texture reads
#ifndef MATERIAL_DECODE_CUSTOM_TEXTURES

#if !defined(USE_MIPMAPPING) && !defined(NO_TEXTURE_GRAD)
    #define NO_TEXTURE_GRAD
#endif

#ifdef NO_TEXTURE_GRAD
float material_texture_bias = 0.0f;
#endif

inline float textured_channel_dynamic(const vec4 x, uint32_t channel) {
    if (channel == 0)
        return x[0];
    else if (channel == 1)
        return x[1];
    else if (channel == 2)
        return x[2];
    else // if (channel == 3)
        return x[3];
}

inline vec4 textured_color_param(const vec4 x, GLSL_in(HitPoint) hit) {
    const uint32_t mask = floatBitsToUint(x.x);
    if (IS_TEXTURED_PARAM(mask) != 0) {
        const uint32_t tex_id = GET_TEXTURE_ID(mask);
#ifndef NO_TEXTURE_GRAD
        return textureGrad(SCENE_GET_TEXTURE(tex_id), hit.uv, hit.duvdxy[0], hit.duvdxy[1]);
#else
        return textureLod(SCENE_GET_TEXTURE(tex_id), hit.uv, material_texture_bias);
#endif
    }
    return x;
}
inline float textured_scalar_param(const float x, GLSL_in(HitPoint) hit) {
    const uint32_t mask = floatBitsToUint(x);
    if (IS_TEXTURED_PARAM(mask) != 0) {
        const uint32_t tex_id = GET_TEXTURE_ID(mask);
        const uint32_t channel = GET_TEXTURE_CHANNEL(mask);
#ifndef NO_TEXTURE_GRAD
        return textured_channel_dynamic(
            textureGrad(SCENE_GET_TEXTURE(tex_id), hit.uv, hit.duvdxy[0], hit.duvdxy[1]), channel);
#else
        return textured_channel_dynamic(
            textureLod(SCENE_GET_TEXTURE(tex_id), hit.uv, material_texture_bias), channel);
#endif
    }
    return x;
}

// accessors for potentially unrolled standard textures
#ifdef UNROLL_STANDARD_TEXTURES

#define get_standard_texture_sampler(tex_id, material_id, slot) \
    SCENE_GET_STANDARD_TEXTURE(material_id * STANDARD_TEXTURE_COUNT + slot)

inline vec4 textured_standard_param(uint32_t material_id, GLSL_in(HitPoint) hit, int slot) {
#ifndef NO_TEXTURE_GRAD
    return textureGrad(get_standard_texture_sampler(ignored, material_id, slot), hit.uv, hit.duvdxy[0], hit.duvdxy[1]);
#else
    return textureLod(get_standard_texture_sampler(ignored, material_id, slot), hit.uv, material_texture_bias);
#endif
}

#else

#ifndef get_standard_texture_sampler
#define get_standard_texture_sampler(tex_id, material_id, slot) \
    SCENE_GET_TEXTURE(tex_id)
#endif

#endif

#endif // ifndef MATERIAL_DECODE_CUSTOM_TEXTURES

// additional parameters loaded by:
// inline void load_material(inout MATERIAL_TYPE mat, MATERIAL_PARAMS p, in const vec2 uv, in const mat2 duvdxy);

// default attributes in `mat` that are filled automatically:
// vec3 base_color, float metallic, float roughness, float ior, vec3 specular_transmission
inline float unpack_material(GLSL_inout(MATERIAL_TYPE) mat, GLSL_out(EmitterInteraction) emitter
    , uint32_t material_id, GLSL_in(MATERIAL_PARAMS) p, GLSL_in(HitPoint) hit) {
    // load default material parameters (fast paths!)
    vec4 texel = textured_color_standard_param(material_id, vec4(p.base_color, 1.0f), hit, STANDARD_TEXTURE_BASECOLOR_SLOT);
    float alpha = texel.a;
    mat.base_color = vec3(texel);
#ifdef PREMULTIPLIED_BASE_COLOR_ALPHA
    if (alpha > 0.001f)
        mat.base_color /= alpha;
#endif

#ifndef SIMPLIFIED_MATERIAL
#ifdef UNROLL_STANDARD_TEXTURES
    // fast path ensuring fused texture sampling
    vec3 specular = vec3(textured_color_standard_param(material_id, vec4(p.specular, p.roughness, p.metallic, 0.0f), hit, STANDARD_TEXTURE_SPECULAR_SLOT));
    if (IS_TEXTURED_PARAM(floatBitsToUint(p.specular)) != 0)
        mat.specular = specular.x;
    else
        mat.specular = p.specular;
    mat.roughness = specular.y;
    mat.metallic = specular.z;
#else
    mat.specular = textured_scalar_standard_param(material_id, p.specular, hit, STANDARD_TEXTURE_SPECULAR_SLOT, 0);
    mat.roughness = textured_scalar_standard_param(material_id, p.roughness, hit, STANDARD_TEXTURE_SPECULAR_SLOT, 1);
    mat.metallic = textured_scalar_standard_param(material_id, p.metallic, hit, STANDARD_TEXTURE_SPECULAR_SLOT, 2);
#endif
    mat.ior = textured_scalar_param(p.ior, hit);
#endif

    emitter.radiance = p.base_color * p.emission_intensity;
    if (p.emission_intensity != 0.0f) {
        if (IS_TEXTURED_PARAM(floatBitsToUint(p.base_color.x)) != 0)
            emitter.radiance = mat.base_color * p.emission_intensity;
        mat.base_color = vec3(0.0f);
    }

    // load remaining material parameters
    load_material(mat, p, hit, alpha);

    return alpha;
}

inline float get_material_alpha(uint32_t material_id, GLSL_in(MATERIAL_PARAMS) p, GLSL_in(HitPoint) hit)
#ifndef CUSTOM_MATERIAL_ALPHA
#define get_base_material_alpha get_material_alpha
#else
; inline float get_base_material_alpha(uint32_t material_id, GLSL_in(MATERIAL_PARAMS) p, GLSL_in(HitPoint) hit)
#endif
{
    return textured_color_standard_param(material_id, vec4(p.base_color, 1.0f), hit, STANDARD_TEXTURE_BASECOLOR_SLOT).a;
}

#endif
