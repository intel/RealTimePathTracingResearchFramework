// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#extension GL_EXT_shader_atomic_float : require

// Available feature flags:
// #define ENABLE_RAYQUERIES
// #define ENABLE_AOV_BUFFERS

// requires: #define AOV_TARGET_PIXEL <ivec2>

layout(binding = ACCUMBUFFER_BIND_POINT, set = 0, rgba32f) uniform writeonly image2D accum_buffer;
#ifdef ATOMIC_ACCUMULATE_ADD
layout(binding = ATOMIC_ACCUMBUFFER_BIND_POINT, set = 0, r32f) uniform volatile image2DArray atomic_accum_buffer;
#else
layout(binding = ATOMIC_ACCUMBUFFER_BIND_POINT, set = 0, r32ui) uniform volatile uimage2DArray atomic_accum_buffer;
#endif

#ifdef ENABLE_AOV_BUFFERS
layout(binding = AOV_ALBEDO_ROUGHNESS_BIND_POINT, set = 0, rgba16f) uniform writeonly image2D aov_albedo_roughness_buffer;
layout(binding = AOV_NORMAL_DEPTH_BIND_POINT, set = 0, rgba16f) uniform writeonly image2D aov_normal_depth_buffer;
layout(binding = AOV_MOTION_JITTER_BIND_POINT, set = 0, rgba16f) uniform writeonly image2D aov_motion_jitter_buffer;
#endif

#ifdef ENABLE_RAYQUERIES
layout(binding = RAYRESULTS_BIND_POINT, set = QUERY_BIND_SET, std430) buffer RayResultsBuf {
    vec4 ray_results[];
};
#endif

#ifdef ENABLE_RAYQUERIES
void accumulate_query(uint query_id, vec4 new_result, uint sample_index) {
    vec4 accum_color = (sample_index > 0) ? ray_results[query_id] : vec4(0);
    accum_color += (new_result - accum_color) / (sample_index + 1);

    // todo: atomic support required?
    if (sample_index == 0)
        ray_results[query_id] = accum_color;
    else
        ray_results[query_id] += accum_color;
}
#endif

void accumulate(ivec2 fb_pixel, vec4 new_result, uint sample_index, bool accumulation_atomic) {
#ifdef ATOMIC_ACCUMULATE
    if (accumulation_atomic) {
#ifdef ATOMIC_ACCUMULATE_ADD
        UNROLL_FOR (int c = 0; c < 4; ++c) {
            imageAtomicAdd(atomic_accum_buffer, ivec3(fb_pixel, c), new_result[c]);
        }
#else
        // todo: adding this currently causes perf impact on DG2 even if not executed
        UNROLL_FOR (int c = 0; c < 4; ++c) {
            uint found = imageLoad(atomic_accum_buffer, ivec3(fb_pixel, c)).x;
            uint expected;
            do {
                float current = uintBitsToFloat(found);
                float accum = (sample_index > 0) ? current : 0.0f;
                accum += (new_result[c] - accum) / (sample_index + 1);
                expected = found;
                found = imageAtomicCompSwap(atomic_accum_buffer, ivec3(fb_pixel, c), expected, floatBitsToUint(accum));
            } while (found != expected);
        }
#endif
    }
    else
#endif
    {
        //vec4 accum_color = (sample_index > 0) ? imageLoad(accum_buffer, fb_pixel) : vec4(0);
        //accum_color += (new_result - accum_color) / (sample_index + 1);
        vec4 accum_color = new_result;
        imageStore(accum_buffer, fb_pixel, accum_color);
    }
}

void store_motion_jitter_aovs(vec3 position, vec3 motion_vector) {
#ifdef ENABLE_AOV_BUFFERS
    ivec2 fb_pixel = AOV_TARGET_PIXEL;
    vec4 ref_proj = view_params.VP_reference * vec4(position + motion_vector, 1.0f);
    vec2 ref_point = ref_proj.xy / max(ref_proj.w, 0.0f);
    vec4 cur_proj = view_params.VP * vec4(position, 1.0f);
    vec2 cur_point = cur_proj.xy / max(cur_proj.w, 0.0f);
    //vec2 cur_point = (vec2(fb_pixel) + vec2(0.5f)) / vec2(view_params.frame_dims) * 2.0f - vec2(1.0f);
    //imageStore(aov_motion_jitter_buffer, fb_pixel, ref_point.xy, view_params.screen_jitter);
    imageStore(aov_motion_jitter_buffer, fb_pixel, vec4(ref_point.xy - cur_point.xy, view_params.screen_jitter));
#endif
}

void store_geometry_aovs(vec3 normal, vec3 hit_point, vec3 motion_vector) {
#ifdef ENABLE_AOV_BUFFERS
    ivec2 fb_pixel = AOV_TARGET_PIXEL;
    float depth = length(hit_point - view_params.cam_pos);
    imageStore(aov_normal_depth_buffer, fb_pixel, vec4(normal, depth));
#endif
    store_motion_jitter_aovs(hit_point, motion_vector);
}

void store_material_aovs(vec3 albedo, float roughness, float ior) {
#ifdef ENABLE_AOV_BUFFERS
    ivec2 fb_pixel = AOV_TARGET_PIXEL;
    imageStore(aov_albedo_roughness_buffer, fb_pixel, vec4(albedo, ior != 1.0f ? roughness : 1.0f));
#endif
}
