// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct RenderGraphic {
    enum AOVBufferIndex {
        AOVAlbedoRoughnessIndex = 0,
        AOVNormalDepthIndex,
        AOVMotionJitterIndex,
        AOVBufferCount
    };

    static const int MAX_SWAP_BUFFERS = 2;
    static int DEFAULT_SWAP_BUFFER_COUNT;

    RenderGraphic() = default;
    RenderGraphic(const RenderGraphic &) = delete;
    RenderGraphic& operator=(const RenderGraphic &) = delete;
    virtual ~RenderGraphic() = default;

    /* Returns (width, height, channels) */
    virtual glm::uvec3 get_framebuffer_size() const { return glm::uvec3{0u,0u,0u}; }

    virtual size_t readback_framebuffer(size_t bufferSize,
                                        unsigned char *buffer,
                                        bool force_refresh = false) { return 0; };

    // returns true, if supported & successful
    virtual size_t readback_framebuffer(size_t bufferSize,
                                        float *buffer,
                                        bool force_refresh = false) { return 0; }

    // These are half float buffers!
    virtual size_t readback_aov(AOVBufferIndex aovIndex,
                                size_t bufferSize,
                                uint16_t *buffer,
                                bool force_refesh = false) { return 0; }
};

#ifdef GL_TRUE
struct RenderGLGraphic {
    GLuint display_texture = -1;

    virtual ~RenderGLGraphic() = default;
};
#endif
