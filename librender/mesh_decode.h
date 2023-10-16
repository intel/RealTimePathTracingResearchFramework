// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "mesh.h"

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"
    #include "../librender/dequantize.glsl"
}

void Geometry::get_vertex_positions(glm::vec3* dst_array) const {
    if (format_flags & Geometry::QuantizedPositions) {
        const auto vertices = this->vertices.as_range<uint64_t>();
        auto vertex_curr = vertices.first;
        const auto vertex_last = vertices.last;

        using namespace glm;
        while (vertex_curr != vertex_last) {
            const uint64_t v = *vertex_curr++;
            *dst_array++ = DEQUANTIZE_POSITION(v, this->quantized_scaling, this->quantized_offset);
        }
    } else {
        const auto vertices = this->vertices.as_range<glm::vec3>();
        memcpy(dst_array, vertices.first, sizeof(glm::vec3) * (vertices.last - vertices.first));
    }
}

void Geometry::tri_positions(int tri_idx, glm::vec3& v1, glm::vec3& v2, glm::vec3& v3) const {
    auto indices = glm::uvec3(tri_idx * 3) + glm::uvec3(0, 1, 2);
    if (!(format_flags & Geometry::ImplicitIndices))
        indices = this->indices.data()[tri_idx];
    if (format_flags & Geometry::QuantizedPositions) {
        auto vertices = this->vertices.as_range<uint64_t>().first;
        using namespace glm;
        v1 = DEQUANTIZE_POSITION(
            vertices[indices.x], this->quantized_scaling, this->quantized_offset
        );
        v2 = DEQUANTIZE_POSITION(
            vertices[indices.y], this->quantized_scaling, this->quantized_offset
        );
        v3 = DEQUANTIZE_POSITION(
            vertices[indices.z], this->quantized_scaling, this->quantized_offset
        );
    }
    else {
        auto vertices = this->vertices.as_range<glm::vec3>().first;
        v1 = vertices[indices.x];
        v2 = vertices[indices.y];
        v3 = vertices[indices.z];
    }
}
void Geometry::tri_normals(int tri_idx, glm::vec3& v1, glm::vec3& v2, glm::vec3& v3) const {
    auto indices = glm::uvec3(tri_idx * 3) + glm::uvec3(0, 1, 2);
    if (!(format_flags & Geometry::ImplicitIndices))
        indices = this->indices.data()[tri_idx];
    if (format_flags & Geometry::QuantizedNormalsAndUV) {
        auto nuvs = this->normals.as_range<uint64_t>().first;
        v1 = glsl::dequantize_normal(uint32_t(nuvs[indices.x]));
        v2 = glsl::dequantize_normal(uint32_t(nuvs[indices.y]));
        v3 = glsl::dequantize_normal(uint32_t(nuvs[indices.z]));
    }
    else {
        auto normals = this->normals.as_range<glm::vec3>().first;
        v1 = normals[indices.x];
        v2 = normals[indices.y];
        v3 = normals[indices.z];
    }
}
void Geometry::tri_uvs(int tri_idx, glm::vec2& v1, glm::vec2& v2, glm::vec2& v3) const {
    auto indices = glm::uvec3(tri_idx * 3) + glm::uvec3(0, 1, 2);
    if (!(format_flags & Geometry::ImplicitIndices))
        indices = this->indices.data()[tri_idx];
    if (format_flags & Geometry::QuantizedNormalsAndUV) {
        auto nuvs = this->normals.as_range<uint64_t>().first;
        v1 = glsl::dequantize_uv(uint32_t(nuvs[indices.x] >> 32));
        v2 = glsl::dequantize_uv(uint32_t(nuvs[indices.y] >> 32));
        v3 = glsl::dequantize_uv(uint32_t(nuvs[indices.z] >> 32));
    }
    else {
        auto uvs = this->uvs.as_range<glm::vec2>().first;
        v1 = uvs[indices.x];
        v2 = uvs[indices.y];
        v3 = uvs[indices.z];
    }
}
