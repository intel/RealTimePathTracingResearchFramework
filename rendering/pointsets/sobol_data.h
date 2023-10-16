// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SOBOL_DATA_GLSL
#define SOBOL_DATA_GLSL

#define SobolData_Dimensions 1024
#define SobolData_MatrixSize 32
#define SobolData_TileSize 256
#define SobolData_InvertDim0 0
#define SobolData_InvertDim1 1

struct SobolData
{
    uint32_t matrix[SobolData_Dimensions * SobolData_MatrixSize];
    uint32_t tile_invert_1_0[SobolData_TileSize * SobolData_TileSize];
};

#endif
