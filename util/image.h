// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "file_mapping.h"

enum ColorSpace { LINEAR, SRGB };

struct Image {
    std::string name;
    int width = 0;
    int height = 0;
    int channels = 0;
    mapped_vector<uint8_t> img = {}; // todo: rename to bytes?
    ColorSpace color_space = LINEAR;
    int bcFormat = 0;

    int max_mip_levels() const;
    int mip_levels() const;
    int bits_per_pixel() const;

    static Image fromFile(const std::string &file, const std::string &name, ColorSpace color_space = LINEAR);
    mapped_vector<uint8_t> decompressBytes() const;
    mapped_vector<uint8_t> decompressBytes(Buffer<uint8_t>& scratch) const;
    Image decompress() const;
};

