// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef BN_RANDOM_GLSL
#define BN_RANDOM_GLSL

#include "../language.glsl"

#include "bn_data.h"

#define BN_OPTIMIZED_DIMENSION_REPEAT
// note: 1spp effectively disables Sobol for convergence and just uses BN points as is
// this mostly makes sense for real-time path tracing at 1 spp + denoising
#define BN_OPTIMIZED_SPP 1
#define BN_OPTIMIZED_SPP_SELECT(name) name##_1spp

MAKE_RANDOM_TABLE(BNData, bn_pointset_table)
// e.g.: #define MAKE_RANDOM_TABLE(TableType, tableName) /
// layout(binding = RANDOM_NUMBERS_BIND_POINT, set = 0, scalar) buffer RNBuf { /
//     TableType tableName; /
// };

// Blue-noise dithered sampling based on optimized Sobol sequence as described by:
// "A Low-Discrepancy Sampler that Distributes Monte Carlo Errors as a Blue Noise in Screen Space"
// by Eric Heitz, Laurent Belcour, Victor Ostromoukhov, David Coeurjolly and Jean-Claude Iehl
// https://eheitzresearch.wordpress.com/762-2/
// Also using ideas of dimension padding from:
// "Lessons Learned and Improvements when Building Screen-Space Samplers with Blue-Noise Error Distribution"
// by Laurent Belcour and Eric Heitz
// https://belcour.github.io/blog/research/publication/2021/06/24/sampling_bluenoise_sig21.html
inline float sample_bnd(uint pixelID, uint sampleID, uint d)
{
#ifdef BN_OPTIMIZED_DIMENSION_REPEAT
    uint x_doffset = d / BNData_ScramblingDimensions;
    // shift mask by one pixel to the right every BNData_ScramblingDimensions dimensions
    pixelID = ((pixelID + x_doffset) & uint(BNData_TileSize-1)) + (pixelID & ~uint(BNData_TileSize-1));
    // after BNData_ScramblingDimensions * BNData_TileSize dimensions, move on to unoptimized dimensions
    d = (d & uint(BNData_ScramblingDimensions-1)) + x_doffset / BNData_TileSize * BNData_ScramblingDimensions;
#endif

    d = d & uint(BNData_Dimensions-1);
#ifndef BN_OPTIMIZED_SPP
    sampleID = sampleID & uint(BNData_SampleCount-1);
#else
    // some additional mirroring to avoid mask shifting looking like scrolling
    if ((sampleID & BN_OPTIMIZED_SPP) != 0)
        pixelID ^= uint(BNData_TileSize-1);
    if ((sampleID & (2*BN_OPTIMIZED_SPP)) != 0)
        pixelID ^= uint(BNData_TileSize-1) * BNData_TileSize;
    // shift mask every BN_OPTIMIZED_SPP samples
    uint x_soffset = sampleID / BN_OPTIMIZED_SPP * 73;
    uint y_soffset = sampleID / BN_OPTIMIZED_SPP * 97;
    pixelID = ((pixelID + x_soffset) & uint(BNData_TileSize-1)) + (pixelID & ~uint(BNData_TileSize-1));
    pixelID = ((pixelID + y_soffset * BNData_TileSize) & uint(BNData_TileSize * (BNData_TileSize-1))) + (pixelID & ~uint(BNData_TileSize * (BNData_TileSize-1)));
    // cycle through optimized SPP
    sampleID = sampleID & uint(BN_OPTIMIZED_SPP-1);
#endif

    // xor index based on optimized ranking
    uint rankingIndex = pixelID * BNData_ScramblingDimensions + (d & uint(BNData_ScramblingDimensions-1));
#if BN_OPTIMIZED_SPP > 1
    uint rankedSampleIndex = sampleID ^ bn_pointset_table.BN_OPTIMIZED_SPP_SELECT(tile_ranking_yx_d)[rankingIndex];
#else
    uint rankedSampleIndex = sampleID;
#endif

    // fetch value in sequence
    uint32_t value = bn_pointset_table.sobol_spp_d[d + rankedSampleIndex * BNData_Dimensions];

    // If the dimension is optimized, xor sequence value based on optimized scrambling
    value = value ^ bn_pointset_table.BN_OPTIMIZED_SPP_SELECT(tile_scrambling_yx_d)[rankingIndex];

    // convert to float and return
    return (0.5f + value) / 256.0f;
}

struct BNDState
{
    uint pixelID;
    uint sampleID;
    int dimension;
};

inline BNDState get_bnd_rng(uint index, uint frame, uvec4 pixel_and_dimensions)
{
    uint i = pixel_and_dimensions.x & uint(BNData_TileSize-1);
    uint j = pixel_and_dimensions.y & uint(BNData_TileSize-1);

    BNDState state;
    state.pixelID = i + j * BNData_TileSize;
    state.sampleID = int(index + frame * 13);
    state.dimension = 0;
    return state;
}

inline void pack_bnd_random_state(BNDState rng, out uint state)
{
    state = rng.sampleID | (rng.pixelID << 16);
}

inline BNDState unpack_bnd_random_state(uint random_state, uint index, uint frame)
{
    BNDState state;
    state.pixelID = random_state >> 16;
    state.sampleID = random_state & uint(0xffff);
    state.dimension = -1;
    return state;
}

#define RANDOM_STATE BNDState
#define RANDOM_FLOAT1(state, dim) sample_bnd(state.pixelID, state.sampleID, state.dimension+dim)
#define GET_RNG(index, frame, linear) get_bnd_rng(view_params.frame_id, view_params.frame_offset, linear)
#define RANDOM_SHIFT_DIM(state, dim_offset) (state.dimension += int(dim_offset))
#define RANDOM_SET_DIM(state, dim) (state.dimension = int(dim))

#define PACK_RNG(rng, payload) pack_bnd_random_state(rng, payload.random_state)
#define UNPACK_RNG(payload) unpack_bnd_random_state(payload.random_state, view_params.frame_id, view_params.frame_offset)
#define COMPRESSED_RANDOM_STATE uint32_t random_state;

#endif // RANDOM_GLSL
