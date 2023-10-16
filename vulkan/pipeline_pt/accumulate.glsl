// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "setup_recursive_pt.glsl"

#include "util.glsl"
#include "postprocess/tonemapping_utils.glsl"

#extension GL_EXT_shader_atomic_float : require

// Available feature flags:
// #define ENABLE_RAYQUERIES

layout(binding = FRAMEBUFFER_BIND_POINT, set = 0, rgba8) uniform writeonly image2D framebuffer;

#define AOV_TARGET_PIXEL ivec2(gl_LaunchIDEXT.xy)
#include "../accumulate.glsl"

uint accumulation_index(AccumulationLocation location) {
    return uint(location);
}
uvec2 accumulation_location(AccumulationLocation location) {
    return uvec2(location & 0xffff, location >> 16);
}

void accumulate(AccumulationLocation location, vec4 new_result, uint sample_index) {
#ifdef ENABLE_RAYQUERIES
    if (num_rayqueries > 0) {
        accumulate_query(location, new_result, view_params.frame_id);
        return;
    }
#endif
    //ivec2 pixel = ivec2(accumulation_location(location));
    ivec2 pixel = ivec2(gl_LaunchIDEXT.xy);

    // todo: we should either make this part of a more sensible function signature
    // or just pass everything via payload
    if ((payload.bounce_and_reliability & BOUNCE_FLAG_NOACCUM) != 0)
        new_result.a = 2.0f;

    accumulate(pixel, new_result, view_params.frame_id, (accumulation_flags & ACCUMULATION_FLAGS_ATOMIC) != 0);

#ifdef REPORT_RAY_STATS
    imageStore(ray_stats, pixel, uvec4(ray_count));
#endif
}
