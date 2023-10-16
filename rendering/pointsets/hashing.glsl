// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RNG_HASHING_GLSL
#define RNG_HASHING_GLSL

// Copyright 2009-2020 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
// https://github.com/ospray/ospray/blob/release-2.8.x/ospray/math/random.ih

inline uint32_t murmur_hash3_mix(uint32_t hash, uint32_t k)
{
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint32_t r1 = 15;
    const uint32_t r2 = 13;
    const uint32_t m = 5;
    const uint32_t n = 0xe6546b64;

    k *= c1;
    k = (k << r1) | (k >> (32 - r1));
    k *= c2;

    hash ^= k;
    hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;

    return hash;
}

inline uint32_t murmur_hash3_finalize(uint32_t hash)
{
    hash ^= hash >> 16;
    hash *= 0x85ebca6b;
    hash ^= hash >> 13;
    hash *= 0xc2b2ae35;
    hash ^= hash >> 16;

    return hash;
}

#endif
