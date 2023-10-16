// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#version 460
#extension GL_GOOGLE_include_directive : require

#include "language.glsl"
#include "../gpu_params.glsl"

#include "bsdfs/hit_point.glsl"

#include "rt/materials.glsl"
#include "bsdfs/gltf_bsdf.glsl"

layout(binding = FRAMEBUFFER_BIND_POINT, set = 0, rgba8) uniform writeonly image2D framebuffer;
layout(binding = ACCUMBUFFER_BIND_POINT, set = 0, rgba32f) uniform image2D accum_buffer;

layout(binding = MATERIALS_BIND_POINT, set = 0, scalar) buffer MaterialParamsBuffer {
    MATERIAL_PARAMS material_params[];
};

layout(binding = 0, set = TEXTURE_BIND_SET) uniform sampler2D textures[];
#ifdef STANDARD_TEXTURE_BIND_SET
layout(binding = 0, set = STANDARD_TEXTURE_BIND_SET) uniform sampler2D standard_textures[];
#endif

#define SCENE_GET_TEXTURE(tex_id) textures[tex_id]
#define SCENE_GET_STANDARD_TEXTURE(tex_id) standard_textures[tex_id]

#include "rt/material_textures.glsl"

layout(binding = VIEW_PARAMS_BIND_POINT, set = 0, std140) uniform VBuf {
    LOCAL_CONSTANT_PARAMETERS
};
layout(binding = SCENE_PARAMS_BIND_POINT, set = 0, std140) uniform SBuf {
    GLOBAL_CONSTANT_PARAMETERS
};

layout(binding = 0, set = 1) uniform sampler2D textures[];

#include "../geometry.glsl"

layout(push_constant) uniform PushConstants {
    RenderMeshParams geometry;
};

layout(location = 0) in vec2 pixel_uv;
layout(location = 1) in vec3 pixel_normal;
layout(location = 2) in vec3 pixel_pos; // todo: could be done using compute

layout(location = 0) out vec4 pixel_color;
layout(location = 1) out vec4 albedo_roughness;
layout(location = 2) out vec4 normal_depth;
layout(location = 3) out vec2 motion_vector;

void main() {
    vec3 n = normalize(pixel_normal);
    vec2 uv = pixel_uv;

    // todo
    mat2 duvdxy = mat2(0.0f);

    MATERIAL_TYPE mat;
    EmitterParams emit;
    float material_alpha = unpack_material(mat, emit
        , geometry.material_id, material_params[geometry.material_id]
        , HitPoint(pixel_pos
            , uv, duvdxy, normalize(pixel_pos - view_params.cam_pos))
        );

    // todo some shading, e.g. sun or stochastic sampling ...
    float clampedNdotL = clamp(dot(pixel_normal, scene_params.sun_dir), 0.0, 1.0);
    pixel_color = vec4(mat.base_color * clampedNdotL + emit.radiance, 1.0f);
    albedo_roughness = vec4(mat.base_color, mat.roughness);
    normal_depth = vec4(pixel_normal, length(pixel_pos - view_params.cam_pos));
    
    {
        vec4 ref_proj = view_params.VP_reference * vec4(pixel_pos, 1.0f);
        vec2 ref_point = ref_proj.xy / max(ref_proj.w, 0.0f);
        vec4 cur_proj = view_params.VP * vec4(pixel_pos, 1.0f);
        vec2 cur_point = cur_proj.xy / max(cur_proj.w, 0.0f);
        motion_vector = ref_point - cur_point;
    }
}
