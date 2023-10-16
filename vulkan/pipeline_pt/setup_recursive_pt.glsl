// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SETUP_RECURSIVE_PATHTRACER_GLSL
#define SETUP_RECURSIVE_PATHTRACER_GLSL

#include "language.glsl"
#include "../gpu_params.glsl"

#include "../setup_pixel_assignment.glsl"

#define PRIMARY_RAY 0
#define OCCLUSION_RAY 1

// Available feature flags:
// #define ENABLE_RAYQUERIES

// #define NON_RECURSIVE
// #define STACK_RECURSIVE
// #define TAIL_RECURSIVE

// default to TAIL_RECURSIVE
#if !defined(NON_RECURSIVE) && !defined(STACK_RECURSIVE)
    #undef TAIL_RECURSIVE
    #define TAIL_RECURSIVE
#endif

#ifndef TAIL_RECURSIVE
    #define RETURN_TO_RAYGEN
#elif defined(NON_RECURSIVE)
    #error "Incompatible recursion mode selection: NON_RECURSIVE && TAIL_RECURSIVE"
#endif

#include "../gpu_params.glsl"

#define USE_SIMPLIFIED_CAMERA
#include "pathspace.h"
#if DIM_CAMERA_END != DIM_SIMPLE_CAMERA_END
#error "Path space dimension mapping was defined differently previously"
#endif

// select rng for path sampling
#define MAKE_RANDOM_TABLE(TYPE, NAME) \
layout(binding = RANDOM_NUMBERS_BIND_POINT, set = 0, std430) buffer RNBuf {\
    TYPE NAME;\
};
#include "pointsets/selected_rng.glsl"

// define default RNG etc.
#include "defaults.glsl"

#if RBO_lod_technique != LOD_TECHNIQUE_NONE
    #include "../lod/lod_common.glsl"
#endif

#ifdef REPORT_RAY_STATS
layout(binding = RAYSTATS_BIND_POINT, set = QUERY_BIND_SET, r16ui) uniform writeonly uimage2D ray_stats;
#endif

#define AccumulationLocation uint32_t
struct RayPayload {
    uvec2 path_throughput; // contains the output if not tail-recursive (for RETURN_TO_RAYGEN)
    COMPRESSED_RANDOM_STATE
    uint32_t bounce_and_reliability;
    vec3 illum;
    float prev_bsdf_pdf;
#if defined(USE_MIPMAPPING)
    //uvec4 dpdxy; // half precision, interleaved, scaled
    uvec2 footprint;
#endif

#ifdef NON_RECURSIVE
    vec3 next_dir;
    float t;
#endif

// Some LOD techniques require more information about LOD selection in the past to match geometry in future bounces
#if RBO_lod_technique == LOD_TECHNIQUE_MULTI_TLAS_LOD
    uint32_t last_lod; // TO DO: pack more useful data into this field
#endif
#if NEED_LOD_VISUALIZATION
    vec3 lod_vis_color;
#endif
#if RBO_lod_visualize == LOD_VISUALIZE_TS_INVOCATIONS
    uint ts_invocation_counter;
#endif

#if (RBO_lod_technique == LOD_TECHNIQUE_COMPUTE_PRE_PASS) && (RBO_lod_transition == LOD_TRANSITION_STOCHASTIC)
    uint rayMask;
#endif
#if (RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_FULL_PATH || RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_PRIMARY_VISIBILITY)
    float any_hit_count;
#endif
};

void set_throughput(inout RayPayload payload, vec3 throughput, float alpha) {
	payload.path_throughput.x = packHalf2x16(vec2(throughput.x, throughput.y));
    payload.path_throughput.y = packHalf2x16(vec2(throughput.z, alpha));
}
vec4 get_throughput(in RayPayload payload) {
    return vec4( unpackHalf2x16(payload.path_throughput.x), unpackHalf2x16(payload.path_throughput.y) );
}

void setup_payload(out RayPayload payload) {
	payload.path_throughput = uvec2(packHalf2x16(vec2(1.0f)), packHalf2x16(vec2(1.0f, 0.0f)));
    payload.bounce_and_reliability = packHalf2x16(vec2(0.0f, 1.0f));
	payload.illum = vec3(0.0f);
#if NEED_LOD_VISUALIZATION
    payload.lod_vis_color = vec3(0.0f);
#endif
#if RBO_lod_visualize == LOD_VISUALIZE_TS_INVOCATIONS
    payload.ts_invocation_counter = 0;
#endif

#if (RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_FULL_PATH || RBO_debug_mode == DEBUG_MODE_ANY_HIT_COUNT_PRIMARY_VISIBILITY)
    payload.any_hit_count = 0.0;
#endif
}

void set_bounce_and_reliability(inout RayPayload payload, int bounce, float reliability) {
    // note: perf warning: this assumes it is fast to re-read payload here!
    uint32_t bounce_flags = 0xe000 & payload.bounce_and_reliability;
	payload.bounce_and_reliability = packHalf2x16(vec2(0.0f, reliability));
	payload.bounce_and_reliability |= uint32_t(bounce);
    payload.bounce_and_reliability |= bounce_flags;
}
float get_reliability(in RayPayload payload) {
	return unpackHalf2x16(payload.bounce_and_reliability).y;
}
int get_bounce(in RayPayload payload) {
	return int(payload.bounce_and_reliability & 0x1fff);
}
#define BOUNCE_FLAG_NOACCUM 0x2000

layout(push_constant) uniform PushConstants {
    PUSH_CONSTANT_PARAMETERS
};
layout(binding = VIEW_PARAMS_BIND_POINT, set = 0, std140) uniform VPBuf {
    LOCAL_CONSTANT_PARAMETERS
};

AccumulationLocation encode_accumulation_pixel(uvec2 pixel) {
    return AccumulationLocation( (pixel.x & 0xffff) + (pixel.y << 16) );
}

AccumulationLocation encode_accumulation_index(uint index) {
    return AccumulationLocation( index );
}

uvec4 encode_dpdxy(vec3 dpdx, vec3 dpdy, float aux_scale) {
    //float dpdxy_max = max(length(dpdx), length(dpdy));
    //dpdxy_max = max(dpdxy_max, 0.1e-12f);
    mat3x2 m = transpose(mat2x3(dpdx, dpdy));
    return uvec4(
          packHalf2x16(m[0])
        , packHalf2x16(m[1])
        , packHalf2x16(m[2])
        , floatBitsToUint(aux_scale)
    );
}
mat2x3 decode_dpdxy(uvec4 dpdxy, out float aux_scale) {
    aux_scale = uintBitsToFloat(dpdxy.w);
    return transpose(mat3x2(
          unpackHalf2x16(dpdxy.x)
        , unpackHalf2x16(dpdxy.y)
        , unpackHalf2x16(dpdxy.z)
    ));
}

#endif
