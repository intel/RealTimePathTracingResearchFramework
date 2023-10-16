// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

// default to GLSL
#include "language.glsl"

// constants
#ifndef M_PI
    #define M_PI 3.14159265358979323846f
#endif
#ifndef M_1_PI
    #define M_1_PI 0.318309886183790671538f
#endif
#ifndef EPSILON
    #define EPSILON 0.0001f // todo: remove generic epsilon
#endif

#ifndef NEURAL_ENCODING_TYPE
#define NEURAL_ENCODING_TYPE float16_t
#endif

// random number generator
#if !defined(RANDOM_STATE) || !defined(RANDOM_FLOAT1)
    #include "pointsets/lcg_rng.glsl"

    #define RANDOM_STATE LCGRand
    #define RANDOM_FLOAT1(state, dim) lcg_randomf(state)
#endif
#ifndef RANDOM_FLOAT2
    inline vec2 default_ordered_random_float2(GLSL_inout(RANDOM_STATE) state, int dim) {
        vec2 r; // initialize step-by-step to enforce call order
        r.x = RANDOM_FLOAT1(state, dim);
        r.y = RANDOM_FLOAT1(state, dim + 1);
        return r;
    }
    #define RANDOM_FLOAT2(state, dim) default_ordered_random_float2(state, dim)
#endif
#ifndef RANDOM_FLOAT3
    inline vec3 default_ordered_random_float3(GLSL_inout(RANDOM_STATE) state, int dim) {
        // construct step-by-step to enforce call order
        vec2 r2 = RANDOM_FLOAT2(state, dim);
        return vec3(r2, RANDOM_FLOAT1(state, dim + 2));
    }
    #define RANDOM_FLOAT3(state, dim) default_ordered_random_float3(state, dim)
#endif
#ifndef RANDOM_SHIFT_DIM
    #define RANDOM_SHIFT_DIM(state, dim_offset) GLSL_unused(state)
#endif
#ifndef RANDOM_SET_DIM
    #define RANDOM_SET_DIM(state, dim) GLSL_unused(state)
#endif
#ifndef GET_RNG
    #define GET_RNG(index, frame, linear) get_lcg_rng(index, frame, linear)
#endif

#ifndef COMPRESSED_RANDOM_STATE
#define COMPRESSED_RANDOM_STATE uint32_t random_state;
#endif

#ifndef PACK_RNG
    #define PACK_RNG(rng, payload) pack_random_state(rng, payload.random_state)
#endif

#ifndef UNPACK_RNG
    #define UNPACK_RNG(payload) unpack_random_state(payload.random_state)
#endif
