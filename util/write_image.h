// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

enum OutputImageFormat {
  OUTPUT_IMAGE_FORMAT_PNG,
  OUTPUT_IMAGE_FORMAT_PFM,
  OUTPUT_IMAGE_FORMAT_EXR,
};

enum ExrCompression {
    EXR_COMPRESSION_ZIP, // Good general purpose.
    EXR_COMPRESSION_PIZ, // Good with noisy images.
    EXR_COMPRESSION_RLE, // Good with large areas of identical color.
    EXR_COMPRESSION_NONE
};

struct WriteImage
{
    // channels must be 4.
    static bool write_png(const char *filename,
        unsigned width, unsigned height, unsigned channels,
        const unsigned char *pixels);

    // 32 bit float.
    static bool write_pfm(const char *filename,
        unsigned width, unsigned height, unsigned channels,
        const float *pixels);

    // 32 bit float
    static bool write_exr(const char *filename,
        unsigned width, unsigned height, unsigned channels,
        const float *pixels, ExrCompression compression);

    // 16 bit float
    static bool write_exr(const char *filename,
        unsigned width, unsigned height, unsigned channels,
        const uint16_t *pixels, ExrCompression compression);
};
