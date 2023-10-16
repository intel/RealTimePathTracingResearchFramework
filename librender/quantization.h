// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once
#include <glm/glm.hpp>
#include <cstdint>

void dequantize_vertices(void* target, size_t stride, size_t vertexCount
    , void const* source, uint32_t format_flags
    , glm::vec3 quantized_scaling, glm::vec3 quantized_offset);
void dequantize_normals(glm::vec3* target, size_t vertexCount
    , void const* source, uint32_t format_flags);
void dequantize_uvs(glm::vec2* target, size_t vertexCount
    , void const* source, uint32_t format_flags);

template <class T>
inline void dequantize_material_ids(T* target, size_t vertexCount
    , void const* source, uint32_t material_id_bitcount) {
    if (8 * sizeof(T) == material_id_bitcount) {
        memcpy(target, source, sizeof(T) * vertexCount);
        return;
    }
    switch (material_id_bitcount) {
    case 8:
        for (size_t i = 0; i < vertexCount; ++i)
            target[i] = ((uint8_t const*) source)[i];
        break;
    case 16:
        for (size_t i = 0; i < vertexCount; ++i)
            target[i] = ((uint16_t const*) source)[i];
        break;
    case 32:
        for (size_t i = 0; i < vertexCount; ++i)
            target[i] = ((uint32_t const*) source)[i];
        break;
    default:
        assert(false);
    }
}
