// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef BN_DATA_GLSL
#define BN_DATA_GLSL

#define BNData_SampleCount 256
#define BNData_Dimensions 256
#define BNData_ScramblingDimensions 8
#define BNData_TileSize 128

struct BNData
{
    uint32_t sobol_spp_d[BNData_SampleCount * BNData_Dimensions];

    uint32_t tile_scrambling_yx_d_1spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];
    //uint32_t tile_ranking_yx_d_1spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions]; // = { 0 }

    uint32_t tile_scrambling_yx_d_4spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];
    uint32_t tile_ranking_yx_d_4spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];

    uint32_t tile_scrambling_yx_d_16spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];
    uint32_t tile_ranking_yx_d_16spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];

    uint32_t tile_scrambling_yx_d_256spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];
    uint32_t tile_ranking_yx_d_256spp[BNData_TileSize * BNData_TileSize * BNData_ScramblingDimensions];
};

#endif
