// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <glm/glm.hpp>

// sort p into 0x200000u bins [0 .. 0x1FFFFF]
inline uint64_t quantize_position(glm::vec3 p, glm::vec3 extent, glm::vec3 base) {
    p = (p - base) * float(0x200000u) / extent;
    glm::uvec3 u = min(glm::uvec3(p), glm::uvec3(0x1FFFFF));
    return u.x | (uint64_t(u.y) << 21) | (uint64_t(u.z) << 42);
}
// place dequantized p into the centers of 0x200000u bins
inline glm::vec3 dequantization_scaling(glm::vec3 extent) {
    return extent / float(0x200000u);
}
inline glm::vec3 dequantization_offset(glm::vec3 base, glm::vec3 extent) {
    return base + extent * 0.5f / float(0x200000u);
}

// represent 0, -1 and 1 precisely by integers
inline uint32_t quantize_normal(glm::vec3 n) {
    float nl1 = fabs(n.x) + fabs(n.y) + fabs(n.z);
    glm::vec2 pn = glm::vec2(n.x, n.y) / nl1;
    if (n.z <= 0.0f) {
        pn = (glm::vec2(1.0f) - abs(glm::vec2(pn.y, pn.x)))
            * glm::vec2(
                pn.x >= 0.0f ? 1.0f : -1.0f,
                pn.y >= 0.0f ? 1.0f : -1.0f
              );
    }
    pn *= float(0x8000u);
    glm::ivec2 i = clamp(glm::ivec2(pn), glm::ivec2(-0x7FFF), glm::ivec2(0x7FFF));
    glm::uvec2 u = glm::uvec2(glm::ivec2(0x8000u) + i);
    return u.x | (u.y << 16);
}

// tile cleanly by snapping boundaries to integers (wastes 0.5 step on each side)
inline uint32_t quantize_uv(glm::vec2 uv, glm::vec3 safety_offset) {
    uv = glm::vec2(safety_offset.x + uv.x, (1.0f + safety_offset.y) - uv.y) * (float(0xFFFFu) / 8.0f);
    glm::uvec2 u = glm::uvec2(glm::vec2(0.5f) + uv) & glm::uvec2(0xFFFF);
    return u.x | (u.y << 16);
}

inline uint32_t quantize_hdr(glm::vec3 hdr) {
#ifdef __cplusplus
    using glm::max;
#endif
    float m = max(max(1.0f, hdr.x), max(hdr.y, hdr.z));
    int e;
#ifdef __cplusplus
    frexp(m, &e);
#else
    frexp(m, GLSL_to_out_ptr(e));
#endif
    float s = ldexp(1.0f, -e);
    glm::uvec3 q = min(glm::uvec3(hdr * s * 512.0f), glm::uvec3(511));
    return q.z | (q.y << 9) | (q.x << 18) | (uint32_t(e) << 27);
}
