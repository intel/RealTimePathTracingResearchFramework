// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef SOBOL_RNG_GLSL
#define SOBOL_RNG_GLSL

#include "../language.glsl"

#ifndef SOBOL_NO_SCRAMBLE
#include "lcg_rng.glsl"
#endif

#ifdef Z_ORDER_SHUFFLING
#include "sample_order.glsl"
#endif

// The MIT License (MIT)
//
// Copyright (c) 2012 Leonhard Gruenschloss (leonhard@gruenschloss.org)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// These matrices are based on the following publication:
//
// S. Joe and F. Y. Kuo: "Constructing Sobol sequences with better
// two-dimensional projections", SIAM J. Sci. Comput. 30, 2635-2654 (2008).
//
// The tabulated direction numbers are available here:
// http://web.maths.unsw.edu.au/~fkuo/sobol/new-joe-kuo-6.21201

// Compute one component of the Sobol'-sequence, where the component
// corresponds to the dimension parameter, and the index specifies
// the point inside the sequence. The scramble parameter can be used
// to permute elementary intervals, and might be chosen randomly to
// generate a randomized QMC sequence.
#include "sobol_data.h"

// MACROS
MAKE_RANDOM_TABLE(SobolData, sobol_table)
// e.g.: #define MAKE_RANDOM_TABLE(TableType, tableName) /
// layout(binding = RANDOM_NUMBERS_BIND_POINT, set = 0, scalar) buffer RNBuf { /
//     TableType tableName; /
// };

#define RANDOM_STATE SobolRand
#define RANDOM_FLOAT1(state, dim) sobol_randomf(state, dim)

#define RANDOM_SHIFT_DIM(state, dim_offset) sobol_shift_dim(state, dim_offset)
#define RANDOM_SET_DIM(state, dim) sobol_set_dim(state, dim)
#define GET_RNG(index, frame, linear) get_sobol_rng(index, frame, linear)

#define PACK_RNG(rng, payload) (payload.random_state = uvec2(rng.index, rng.scramble.state))
#define UNPACK_RNG(payload) SobolRand(payload.random_state.x, 0, LCGRand(payload.random_state.y))
#define COMPRESSED_RANDOM_STATE uvec2 random_state;

struct SobolRand
{
    uint32_t index;
    uint32_t dimension;
#ifndef SOBOL_NO_SCRAMBLE
    LCGRand scramble;
#endif
};

inline float sobol_point(uint32_t index, uint32_t dimension, uint32_t scramble)
{
    // note: assumes power of 2 in # of dimensions!
    dimension &= uint32_t(SobolData_Dimensions-1);

    uint32_t result = scramble;
    for (uint32_t i = dimension * SobolData_MatrixSize; index != 0; index >>= 1, ++i) {
        if ((index & 1) != 0)
            result ^= sobol_table.matrix[i];
    }

#ifdef Z_ORDER_SHUFFLING
    // fix aligning leading bits in the first two dimensions when matching
    // the next points in the LD sequence by their common pixel coordinate
    // in a virtual SobolData_TileSize x SobolData_TileSize tile
    if (dimension < 2) {
        uint32_t tile_bits = bitCount(SobolData_TileSize - 1);
        // hack(?): Intuitively, what we would want to use are
        // just the varying fractional 0...(31-tile_bits) bits,
        // but they turn out highly correlated in a Z-order sample
        // index arrangement.
        // Therefore, we instead scramble the varying fractional
        // bits using the constant leading bits. Technically, this
        // might count as hash-based Owen Scrambling, but no
        // analysis was done to look at resulting properties,
        // except it fixes noticeable correlation artifacts.
        result ^= (result << tile_bits);
    }
#endif

    return ldexp(float(result), -32);
}

// identifies the next sample overlapping the same pixel as the given sample index
// in a SobolData_TileSize*SobolData_TileSize tile succeeding the given index_shift
inline uint32_t sobol_shift_invert(uint32_t index, uint32_t index_shift)
{
    // we apply the index scrambling and invert the index_shift scrambling in parallel
    index += index_shift;

    uint32_t result_0 = 0;
    uint32_t result_1 = 0;
    for (uint32_t i = 0; index != 0; index >>= 1, ++i) {
        if ((index & 1) != 0) {
            result_0 ^= sobol_table.matrix[SobolData_InvertDim0 * SobolData_MatrixSize + i];
            result_1 ^= sobol_table.matrix[SobolData_InvertDim1 * SobolData_MatrixSize + i];
        }
    }

    uint32_t tile_bits = bitCount(SobolData_TileSize - 1);
    result_0 >>= 32 - tile_bits;
    result_1 >>= 32 - tile_bits;

    return index_shift + sobol_table.tile_invert_1_0[result_1 * SobolData_TileSize + result_0];
}

// identifies a new sample index for the given pixel such that pseudo-random points are
// generated in sobol-ordered tiles of size SobolData_TileSize x SobolData_TileSize
inline uint32_t sobol_sample_id(uint32_t sample_index, uvec2 pixel_id)
{
    uint32_t index_shift = sample_index * SobolData_TileSize * SobolData_TileSize;
    uint32_t tile_bits = bitCount(SobolData_TileSize - 1);
    
    uint32_t index = index_shift;
    uint32_t result_0 = pixel_id.x << (32 - tile_bits);
    uint32_t result_1 = pixel_id.y << (32 - tile_bits);
    for (uint32_t i = 0; index != 0; index >>= 1, ++i) {
        if ((index & 1) != 0) {
            result_0 ^= sobol_table.matrix[SobolData_InvertDim0 * SobolData_MatrixSize + i];
            result_1 ^= sobol_table.matrix[SobolData_InvertDim1 * SobolData_MatrixSize + i];
        }
    }

    result_0 >>= 32 - tile_bits;
    result_1 >>= 32 - tile_bits;
    return index_shift + sobol_table.tile_invert_1_0[result_1 * SobolData_TileSize + result_0];
}

inline SobolRand get_sobol_rng(uint32_t sample_id, uint32_t frame_id, uint32_t linear_id)
{
    SobolRand rng;
    rng.index = sample_id;
    rng.dimension = 0;
#ifndef SOBOL_NO_SCRAMBLE
    rng.scramble = get_lcg_rng(frame_id, 0, linear_id);
#endif
    return rng;
}
inline SobolRand get_sobol_rng(uint32_t sample_id, uint32_t frame_id, uvec4 pixel_and_dimensions)
{
    uint32_t linear;
#ifdef Z_ORDER_SHUFFLING
    // shuffle SobolData_TileSize * SobolData_TileSize Sobol samples per tile
    uint32_t sample_offset = morton_sample_id(0, pixel_and_dimensions.xy, uvec2(SobolData_TileSize), true, false) & (SobolData_TileSize * SobolData_TileSize - 1);
    // find corresponding next Sobol samples in each pixel
    sample_id = sobol_shift_invert(sample_offset, SobolData_TileSize * SobolData_TileSize * sample_id);
    // use different scrambling per tile
    uint32_t tile_bits = bitCount(SobolData_TileSize - 1);
    uvec4 tile_and_counts = pixel_and_dimensions >> tile_bits;
    linear = tile_and_counts.x + tile_and_counts.y * tile_and_counts.z;
#else
    linear = pixel_and_dimensions.x + pixel_and_dimensions.y * pixel_and_dimensions.z; // per-pixel scrambling
#endif

    SobolRand rng;
    rng.index = sample_id;
    rng.dimension = 0;
#ifndef SOBOL_NO_SCRAMBLE
    rng.scramble = get_lcg_rng(frame_id, 0, linear);
#endif
    return rng;
}

inline float sobol_randomf(GLSL_inout(SobolRand) rng, uint32_t dimension)
{
    return sobol_point(rng.index, rng.dimension + dimension
#ifndef SOBOL_NO_SCRAMBLE
        , lcg_random(rng.scramble)
#else
        , 0
#endif
        );
}

inline void sobol_shift_dim(GLSL_inout(SobolRand) rng, uint32_t dim)
{
    rng.dimension += dim;
}

inline void sobol_set_dim(GLSL_inout(SobolRand) rng, uint32_t dim)
{
    rng.dimension = dim;
}

#endif
