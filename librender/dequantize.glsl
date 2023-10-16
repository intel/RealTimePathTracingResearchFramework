// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT


#ifndef DEQUANTIZE_H_GLSL
#define DEQUANTIZE_H_GLSL

#define DEQUANTIZE_POSITION_XYZ(wx, wy, wz, scaling, offset) \
    (vec3( uint(wx) & 0x1FFFFF, \
           uint(wy) & 0x1FFFFF, \
           uint(wz) & 0x1FFFFF) \
      * scaling \
      + offset \
    )
#ifdef GLSL_SPLIT_INT64
#define DEQUANTIZE_POSITION(dword, scaling, offset) \
    DEQUANTIZE_POSITION_XYZ(dword.x, (dword.x >> uint(21)) | (dword.y << uint(11)), dword.y >> uint(10), scaling, offset)
#else
#define DEQUANTIZE_POSITION(dword, scaling, offset) \
    DEQUANTIZE_POSITION_XYZ(dword, dword >> uint(21), dword >> uint(42), scaling, offset)
#endif

inline vec3 dequantize_normal(uint32_t word) {
    vec2 n = vec2(
                ivec2(word & 0xFFFF, word >> 16)
              - ivec2(0x8000)
            ) / float(uint(0x7FFF));
    float nl1 = abs(n.x) + abs(n.y);
    if (nl1 >= 1.0f) {
        n =  (vec2(1.0f) - abs(vec2(n.y, n.x)))
            * vec2(
                n.x >= 0.0f ? 1.0f : -1.0f,
                n.y >= 0.0f ? 1.0f : -1.0f
              );
    }
    return normalize(vec3(
           n.x,
           n.y,
           1.0f - nl1
        ));
}

inline vec2 dequantize_uv(uint32_t word) {
    return vec2(0.0f, 1.0f)
        + vec2(
            ivec2(word & 0xFFFF, -int(word >> 16))
        ) * (8.0f / float(uint(0xFFFF)));
}

inline vec3 dequantize_hdr(uint32_t word) {
    vec3 color = vec3(uvec3(word >> 18, word >> 9, word) & uvec3(0xff)) / 511.0f;
    return ldexp(color, ivec3(word >> 27));
}

#endif
