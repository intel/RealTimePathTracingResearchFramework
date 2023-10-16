// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "image.h"
#include "stb_image.h"

#include <stdexcept>
#include <xmmintrin.h>


int Image::max_mip_levels() const {
    int level_count = 1;
    int w = width;
    int h = height;
    while (w > 1 || h > 1) {
        if (w > 1) w /= 2;
        if (h > 1) h /= 2;
        ++level_count;
    }
    return level_count;
}

int Image::mip_levels() const {
    int level_count = 0;
    size_t remaining_pixels = img.nbytes() * 8 / bits_per_pixel();
    int w = width;
    int h = height;
    assert(remaining_pixels >= (size_t) w * h);
    int bw = this->bcFormat ? 4 : 1;
    while (remaining_pixels > 0) {
        ++level_count;
        int wb = (w + (bw-1)) / bw * bw;
        int hb = (h + (bw-1)) / bw * bw;
        assert(remaining_pixels >= (size_t) wb * hb);
        remaining_pixels -= (size_t) wb * hb;
        if (w > 1) w /= 2;
        if (h > 1) h /= 2;
        assert(w > 1 || h > 1 || remaining_pixels == 0);
    }
    return level_count;
}

int Image::bits_per_pixel() const {
    int blockSize = 16;
    switch (this->bcFormat) {
        case -1: blockSize = 8; break;
        case 1: blockSize = 8; break;
        case 2: break;
        case 3: break;
        case -4: blockSize = 8; break;
        case 4: blockSize = 8; break;
        case -5: break;
        case 5: break;
        default: printf("Unsupported block compression format %d\n", this->bcFormat);  [[fallthrough]];
        case 0: blockSize = 4 * 4 * 4; break;
    }
    return blockSize / 2;  // == blockSize * 8 / (4 * 4)
}

//#if defined(ENABLE_STANDARD_FORMATS) || defined(PBRT_PARSER_ENABLED)
Image Image::fromFile(const std::string &file, const std::string &name, ColorSpace color_space)
{
    Image img = {
        .name = name,
        .color_space = color_space
    };
    stbi_set_flip_vertically_on_load(1);
    uint8_t *data = stbi_load(file.c_str(), &img.width, &img.height, &img.channels, 4);
    if (!data) {
        throw std::runtime_error("Failed to load " + file);
    }
    img.channels = 4; // was converted
    img.img = Buffer<uint8_t>( std::vector<uint8_t>(data, data + img.width * img.height * img.channels) );
    stbi_image_free(data);
    stbi_set_flip_vertically_on_load(0);
    return img;
}
//#endif

