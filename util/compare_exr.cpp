// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <tinyexr.h>

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Image {
    EXRHeader header;
    EXRImage image;
};

Image load_exr(const char *filename)
{
    EXRHeader header = {};
    EXRVersion version = {};
    EXRImage image = {};

    const char *error = nullptr;

    if (ParseEXRHeaderFromFile(&header, &version, filename, &error) < 0) {
        const std::string what(error);
        FreeEXRErrorMessage(error);
        throw std::runtime_error(what);
    } 

    for (int c = 0; c < header.num_channels; ++c) {
        header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
    }

    if (LoadEXRImageFromFile(&image, &header, filename, &error) < 0) {
        const std::string what(error);
        FreeEXRErrorMessage(error);
        FreeEXRHeader(&header);
        throw std::runtime_error(what);
    }

    if (!image.images) {
        FreeEXRImage(&image);
        FreeEXRHeader(&header);
        throw std::runtime_error("Tiled images are not supported.");
    }

    return { header, image };
}

bool compare(const Image &ref, const Image &cmp, const std::string &errImg)
{
    if (ref.image.width != cmp.image.width
     || ref.image.height != cmp.image.height
     || ref.image.num_channels != cmp.image.num_channels)
    {
        std::cerr << "Images must have the same size as the reference image" << std::endl;
        return false;
    }

    const std::size_t numPixels = size_t(ref.image.width) * ref.image.height;

    bool equal = true;

    std::vector<float> errorImage(ref.image.num_channels * numPixels);
    for (int z = 0; z < ref.image.num_channels; ++z) {
        const float *pref = reinterpret_cast<const float *>(ref.image.images[z]);
        const float *pcmp = reinterpret_cast<const float *>(cmp.image.images[z]);
        float *perr = errorImage.data() + z;
        for (size_t p = 0; p < numPixels; ++p, perr += ref.image.num_channels)
        {
            const float vref = pref[p];
            const float vcmp = pcmp[p];

            float relError = 0.f;
            if (vref == 0.f)
                relError = std::fabs(vcmp);
            else
                relError = std::fabs(vref-vcmp) / vref;

            *perr = relError;

            if (relError > 1e-6f)
                equal = false;
        }
    }

    const char *err = nullptr;
    SaveEXR(errorImage.data(), ref.image.width, ref.image.height,
            ref.image.num_channels, 0, errImg.c_str(), &err);
    if (err) {
        std::cerr << err << std::endl;
        FreeEXRErrorMessage(err);
    }

    return equal;
}

int compare(int numFiles, char **files)
{
    assert(numFiles > 1);
    bool error = false;

    std::vector<Image> images;
    images.reserve(static_cast<size_t>(numFiles));
    for (int i = 0; i < numFiles; ++i) {
        try {
            images.push_back(load_exr(files[i]));
        } catch (const std::runtime_error &e) {
            std::cerr << e.what() << std::endl;
            error = true;
        }
    }

    if (!error) {
        for (size_t i = 1; i < images.size(); ++i) {
            std::cout << "Comparing " << files[i] << " with " << files[0] << std::endl;
            const bool is_equal = compare(images[0], images[i], std::string(files[i])+"_err.exr");
            if (!is_equal) {
                std::cerr << files[i] << " isn't the same as " << files[0] << std::endl;
                error = true;
            }
        }
    }

    for (Image &img: images) {
        FreeEXRImage(&img.image);
        FreeEXRHeader(&img.header);
    }

    return error ? -1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " REF CMP [CMP...]" << std::endl;
        return -1;
    }

    return compare(argc-1, argv+1);
}

