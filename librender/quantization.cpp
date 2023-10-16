// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "quantization.h"
#include "mesh.h"
#include <cstring>

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"
    #include "../librender/dequantize.glsl"
}
#include "../librender/quantize.h"

void dequantize_vertices(void* target, size_t stride, size_t vertexCount
    , void const* source, uint32_t format_flags
    , glm::vec3 quantized_scaling, glm::vec3 quantized_offset) {
    char* target_bytes = (char*) target;
    if (format_flags & Geometry::QuantizedPositions) {
        uint64_t const* quantized_vertices = (uint64_t const*) source;
        for (size_t i = 0; i < vertexCount; ++i) {
            using namespace glm;
            *(vec3*) target_bytes = DEQUANTIZE_POSITION(
                quantized_vertices[i], quantized_scaling, quantized_offset
            );
            target_bytes += stride;
        }
    }
    else if (stride == sizeof(glm::vec3))
        std::memcpy(target, source, stride * vertexCount);
    else {
        glm::vec3 const* unquantized_vertices = (glm::vec3 const*) source;
        for (size_t i = 0; i < vertexCount; ++i) {
            *(glm::vec3*) target_bytes = unquantized_vertices[i];
            target_bytes += stride;
        }
    }
}

void dequantize_normals(glm::vec3* target, size_t vertexCount
    , void const* source, uint32_t format_flags) {
    if (format_flags & Geometry::QuantizedNormalsAndUV) {
        uint64_t const* quantized_vertices = (uint64_t const*) source;
        for (size_t i = 0; i < vertexCount; ++i)
            target[i] = glsl::dequantize_normal(quantized_vertices[i]);
    }
    else
        std::memcpy(target, source, sizeof(glm::vec3) * vertexCount);
}

void dequantize_uvs(glm::vec2* target, size_t vertexCount
    , void const* source, uint32_t format_flags) {
    if (format_flags & Geometry::QuantizedNormalsAndUV) {
        uint64_t const* quantized_vertices = (uint64_t const*) source;
        for (size_t i = 0; i < vertexCount; ++i)
            target[i] = glsl::dequantize_uv(uint32_t(quantized_vertices[i] >> 32));
    }
    else
        std::memcpy(target, source, sizeof(glm::vec2) * vertexCount);
}
