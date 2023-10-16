// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef LCG_RNG_GLSL
#define LCG_RNG_GLSL

#include "../language.glsl"
#include "hashing.glsl"

// https://github.com/ospray/ospray/blob/master/ospray/math/random.ih
struct LCGRand {
    uint32_t state;
};

inline uint32_t lcg_random(GLSL_inout(LCGRand) rng)
{
    const uint32_t m = 1664525;
    const uint32_t n = 1013904223;
    rng.state = rng.state * m + n;
    return rng.state;
}

inline float lcg_randomf(GLSL_inout(LCGRand) rng)
{
	return ldexp(float(lcg_random(rng)), -32);
}

inline LCGRand get_lcg_rng(uint32_t index, uint32_t frame, uint32_t linear)
{
    LCGRand rng;
    rng.state = murmur_hash3_mix(frame, linear); // note: low-quality randomization w.r.t. frame (not used for progressive rendering/temporal accumulation). should rename "frame" to "shot"
    rng.state = murmur_hash3_mix(rng.state, index);
    rng.state = murmur_hash3_finalize(rng.state);
    return rng;
}
inline LCGRand get_lcg_rng(uint32_t index, uint32_t frame, uvec4 pixel_and_dimensions)
{
    return get_lcg_rng(index, frame, pixel_and_dimensions.x + pixel_and_dimensions.y * pixel_and_dimensions.z);
}

inline void pack_random_state(LCGRand rng, GLSL_out(uint32_t) state)
{
    state = rng.state;
}

inline LCGRand unpack_random_state(uint32_t random_state)
{
    LCGRand rng;
    rng.state = random_state;
    return rng;
}

#endif

