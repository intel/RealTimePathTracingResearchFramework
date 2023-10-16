// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "write_image.h"
#include "stb_image_write.h"
#include "tinyexr.h"
#include "error_io.h"

#include <cstdio>
#include <string>
#include <vector>

bool WriteImage::write_png(const char *filename,
    unsigned width, unsigned height, unsigned channels,
    const unsigned char *pixels)
{
    if (width == 0 || height == 0 || channels != 4 || !pixels) {
        print(CLL::CRITICAL, "Invalid image passed to write_png\n");
        return false;
    }

    std::string path{filename};
    path += ".png";

    if (1 != stbi_write_png(path.c_str(),
        width, height, channels, pixels, channels * width))
    {
        print(CLL::CRITICAL, "Failed to write %s\n", path.c_str());
        return false;
    }
    return true;
}

bool WriteImage::write_pfm(const char *filename,
    unsigned width, unsigned height, unsigned channels,
    const float *pixels)
{
    if (width == 0 || height == 0 || channels < 3 || !pixels) {
        print(CLL::CRITICAL, "Invalid image passed to write_pfm\n");
        return false;
    }

    std::string path{filename};
    path += ".pfm";

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        print(CLL::CRITICAL, "Failed to write %s\n", path.c_str());
        return false;
    }
    fprintf(f, "PF\n%i %i\n-1.0\n", width, height);
    const unsigned ie = width * height;
    const float* p = pixels;
    std::vector<float> rgb(ie * 3);
    for (unsigned y = 0; y < height; y++){
        for (unsigned x = 0; x < width; x++){
            unsigned i_src = width * y + x;
            unsigned i_dest =  width * (height - y - 1) + x;
            for (unsigned j = 0; j < 3; ++j)
                rgb[i_dest * 3 + j] = p[i_src * channels + j];
        }
    }
    fwrite(rgb.data(), sizeof(float), rgb.size(), f);
    fclose(f);
    return true;
}

template <class T>
struct PixelType;

template <>
struct PixelType<float>
{
    static constexpr int pixel_type = TINYEXR_PIXELTYPE_FLOAT;
};

template <>
struct PixelType<uint16_t>
{
    static constexpr int pixel_type = TINYEXR_PIXELTYPE_HALF;
};

template <class T>
std::vector<T> separate_interleaved_channels(size_t num_pixels, size_t num_channels, const T* src)
{
    std::vector<T> sep(num_channels * num_pixels);
    for (size_t c = 0; c < num_channels; ++c)
    {
        T *tgt = sep.data() + c*num_pixels;
        for (size_t i = 0; i < num_pixels; ++i) {
            // Flip for ABGR order!
            tgt[i] = src[num_channels * i + c];
        }
    }
    return sep;
}

template <class T>
static bool write_exr_generic(const char *filename, size_t width,
        size_t height, size_t channels,
        const T *pixels, ExrCompression compression)
{
    constexpr size_t num_channels = 4;
    if (width == 0 || height == 0 || channels != num_channels || !pixels) {
        print(CLL::CRITICAL, "Invalid image passed to write_exr\n");
        return false;
    }

    const size_t num_pixels = width * height;

    std::string path{filename};
    path += ".exr";

    // EXR expects the channels to be separated.
    std::vector<T> separated = separate_interleaved_channels(num_pixels,
        num_channels, pixels);
    T* img_ptrs[num_channels];
    img_ptrs[0] = separated.data() + 3 * num_pixels; // Flip order!
    img_ptrs[1] = separated.data() + 2 * num_pixels;
    img_ptrs[2] = separated.data() + 1 * num_pixels;
    img_ptrs[3] = separated.data();

    // Note: Channels must be in alphabetical order.
    EXRChannelInfo channel_info[num_channels];
    memset(channel_info, 0, sizeof(EXRChannelInfo)*num_channels);

    channel_info[0].name[0] = 'A';
    channel_info[1].name[0] = 'B';
    channel_info[2].name[0] = 'G';
    channel_info[3].name[0] = 'R';

    int pixel_types[] = { PixelType<T>::pixel_type,
                          PixelType<T>::pixel_type,
                          PixelType<T>::pixel_type,
                          PixelType<T>::pixel_type };

    EXRHeader header;
    InitEXRHeader(&header);
    header.num_channels = num_channels;
    header.channels = channel_info;
    header.pixel_types = pixel_types;
    header.requested_pixel_types = pixel_types;

    // Compression is slow; >1s for a ZIP compressed full hd image, or 0.5 s
    // for PIZ compressed. So if possible this should be done in a post process.
    switch (compression) {
        case EXR_COMPRESSION_ZIP:
            header.compression_type = TINYEXR_COMPRESSIONTYPE_ZIP;
            break;
        case EXR_COMPRESSION_PIZ:
            header.compression_type = TINYEXR_COMPRESSIONTYPE_PIZ;
            break;
        case EXR_COMPRESSION_RLE:
            header.compression_type = TINYEXR_COMPRESSIONTYPE_RLE;
            break;
        default:
            header.compression_type = TINYEXR_COMPRESSIONTYPE_NONE;
            break;
    }

    EXRImage image;
    InitEXRImage(&image);
    image.num_channels = num_channels;
    image.images = reinterpret_cast<unsigned char**>(img_ptrs);
    image.width = width;
    image.height = height;

    const int ret = SaveEXRImageToFile(&image, &header, path.c_str(), nullptr);
    if (ret != TINYEXR_SUCCESS) {
        print(CLL::CRITICAL, "Failed to write %s\n", path.c_str());
    }
    return ret == TINYEXR_SUCCESS;
}

bool WriteImage::write_exr(const char *filename,
    unsigned width, unsigned height, unsigned channels,
    const float *pixels, ExrCompression compression)
{
    return write_exr_generic(filename, width, height, channels, pixels, compression);
}

bool WriteImage::write_exr(const char *filename,
    unsigned width, unsigned height, unsigned channels,
        const uint16_t *pixels, ExrCompression compression)
{
    return write_exr_generic(filename, width, height, channels, pixels, compression);
}
