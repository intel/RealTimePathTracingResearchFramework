// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef POINTSETS_SAMPLE_ORDER_GLSL
#define POINTSETS_SAMPLE_ORDER_GLSL

#include "../util.glsl"
#include "lcg_rng.glsl"

uint32_t padded_sample_id(uint32_t sample_id, uvec2 pixel_id, uvec2 pixel_dims) {
	uvec2 padded_pixel_dims = uvec2(1) << findMSB(pixel_dims);
	padded_pixel_dims = padded_pixel_dims << uvec2(notEqual(padded_pixel_dims, pixel_dims));
	return sample_id * (padded_pixel_dims.x * padded_pixel_dims.y) + pixel_id.y * padded_pixel_dims.x + pixel_id.x;
}

// loosely based on "Screen-Space Blue-Noise Diffusion of Monte Carlo Sampling Error via Hierarchical Ordering of Pixels"
// by Abdalla G. M. Ahmed and Peter Wonka
// assigns consecutive sample IDs in Z-order tiles of size `tile_dims`, with random Z orientation shuffling.
// An (optional) sample ID offset for optional additional randomization can be provided, and orientation
// can optionally be randomized uniquely per tile.
uint32_t morton_sample_id(uint32_t sample_id, uvec2 pixel_id, uvec2 tile_dims, bool hash_tile_id, bool hash_sample_id) {
	// pad to next power of two to contain all pixel_id bits
	uvec2 padded_tile_dims = uvec2(1) << findMSB(tile_dims);
	padded_tile_dims = padded_tile_dims << uvec2(notEqual(padded_tile_dims, tile_dims));
	uint padded_tile_pcount = padded_tile_dims.x * padded_tile_dims.y;

	// assemble morton code
	uint padded_parted_id_x = Part1By1(pixel_id.x);
	uint padded_parted_id_y = Part1By1(pixel_id.y);
	uint padded_linear_id = (padded_parted_id_y << 1) + padded_parted_id_x;

	// trim bit interleaving to the range of bits present in both dimensions
	uint padded_min_dim_mask = (padded_tile_dims.x - 1) & (padded_tile_dims.y - 1);
	uint padded_interleaved_mask = (padded_min_dim_mask + 1) * (padded_min_dim_mask + 1) - 1;
	padded_linear_id &= padded_interleaved_mask;
	// concatenate bits that are only present in one of the dimensions
	padded_linear_id |= ((pixel_id.x | pixel_id.y) & ~padded_min_dim_mask) * (padded_min_dim_mask + 1);

	// filter out tile indexing before scrambling
	if (!hash_tile_id)
		padded_linear_id &= padded_tile_pcount - 1;

	uint scambled_linear_id = padded_linear_id;
	// prepare bit vector that allows for swapping of interleaved dimension pairs
	uint padded_linear_id_swap = padded_parted_id_x ^ padded_parted_id_y;
	padded_linear_id_swap |= padded_linear_id_swap << 1;

	// configure randomization (stay inside min. square tile)
	uint scramble_mask = padded_interleaved_mask;
	uint sample_hash = hash_sample_id ? murmur_hash3_mix(0, sample_id) : 0;
	for (uint ie = 2 * findMSB(padded_min_dim_mask + 1); ie > 0; ) {
		// random permutation for bit pair at `ie-2` based on bits succeeding `ie`
		uint perm = murmur_hash3_finalize(murmur_hash3_mix(sample_hash, (padded_linear_id >> ie)));
		// perform random permutation and randomized bit swap
		bool swap = (perm & 0x4) != 0;
		perm &= 0x3;

		// permute bit pair at `ie-2`, if contained in the randomization mask
		ie -= 2;
		scambled_linear_id ^= (perm << ie) & scramble_mask;
		
		// swap bit pair, if contained in the scramble mask
		uint swap_mask = swap ? (0x3u << ie) : 0;
		if (swap_mask == (scramble_mask & swap_mask))
			scambled_linear_id ^= padded_linear_id_swap & swap_mask;
	}

	// filter out tile indexing after scrambling
	if (hash_tile_id)
		scambled_linear_id &= padded_tile_pcount - 1;
	
	return sample_id * padded_tile_pcount + scambled_linear_id;
}

#endif
