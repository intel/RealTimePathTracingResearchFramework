// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <cstdio>
#include <cstdint>
#include <vector>
#include "sobol_tables_src.h"

int main(int argc, char const* const* argv) {
    int sample_bits = 32;
    if (argc > 1)
        sscanf(argv[1], "%i", &sample_bits);

    int tile_size = 256;
    if (argc > 2)
        sscanf(argv[2], "%i", &tile_size);

    int dim_x = 0;
    int dim_y = 1;
    if (argc > 3)
        sscanf(argv[3], "%i", &dim_x);
    if (argc > 4)
        sscanf(argv[4], "%i", &dim_y);

    printf("SobolMatrix[%d][%d] = {\n", SobolDimensions, sample_bits);
    for (int i = 0; i < SobolDimensions; ++i) {
        for (int j = 0; j < sample_bits; ++j)
            printf("    0x%08xU,\n", (unsigned) sobol_matrix[i * SobolMatrixSize + j]);
        printf("\n");
    }
    printf("}\n");

    uint32_t tile_bits = 0;
    for (uint32_t m = uint32_t(tile_size - 1); m; m >>= 1)
        ++tile_bits;
    
    std::vector<uint32_t> inversion_table_y_x(tile_size * tile_size);
    for (int i = 0, ie = tile_size * tile_size; i < ie; ++i) {
        uint32_t result_x = 0;
        uint32_t result_y = 0;
        
        // evaluate sobol point
        {
            uint32_t index = (uint32_t) i;
            for (uint32_t i = 0; index != 0; index >>= 1, ++i) {
                if ((index & 1) != 0) {
                    result_x ^= sobol_matrix[dim_x * SobolMatrixSize + i];
                    result_y ^= sobol_matrix[dim_y * SobolMatrixSize + i];
                }
            }
        }
        
        //bool round_up_x = (result_x << tile_bits) > 0x80000000u;
        //bool round_up_y = (result_y << tile_bits) > 0x80000000u;

        result_x >>= 32 - tile_bits; //result_x += uint32_t(round_up_x);
        result_y >>= 32 - tile_bits; //result_y += uint32_t(round_up_y);

        inversion_table_y_x[result_y * tile_size + result_x] = i;
    }

    printf("SobolInversion_%d_%d[%d][%d] = {\n", dim_y, dim_x, tile_size, tile_size);
    for (int i = 0, k = 0; i < tile_size; ++i) {
        printf("   ");
        for (int j = 0; j < tile_size; ++j, ++k)
            printf(" %u,", (unsigned) inversion_table_y_x[k]);
        printf("\n");
    }
    printf("}\n");

    // sanity check
    int num_zeros = 0;
    for (auto x : inversion_table_y_x)
        num_zeros += (x == 0);

    printf("// Tile bits: %d; Dimensions: %d %d; Zeros: %d\n\n", tile_bits, dim_x, dim_y, num_zeros);
}
