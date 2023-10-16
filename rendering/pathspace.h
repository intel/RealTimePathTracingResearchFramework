// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef PATHSPACE_H
#define PATHSPACE_H

// for now just a bunch of defines to map pointsets dimensions to path space
// usually projections are optimized for 2D, so the right coupling is important
// and the dimensions used should be a multiple of 2

// Camera dimensions for a full-spec path tracer, not used for now
#define DIM_LAMBDA      2
#define DIM_TIME        3
#define DIM_PIXEL_X     0
#define DIM_PIXEL_Y     1
#define DIM_APERTURE_X  4
#define DIM_APERTURE_Y  5
#define DIM_CAMERA_END  6 // number of dimensions used for the camera setup

// simple cameras only use 2 resp. 4 dimensions
#define DIM_SIMPLE_CAMERA_BEGIN DIM_PIXEL_X
#define DIM_SIMPLE_CAMERA_END DIM_PIXEL_X+2

// vertex dimensions used
#define DIM_DIRECTION_X 0
#define DIM_DIRECTION_Y 1
#define DIM_LOBE        2
#define DIM_FREE_PATH   3
#define DIM_VERTEX_END  4

#define DIM_RR          (DIM_FREE_PATH-DIM_VERTEX_END) // using the currently unused free path slot before vertex end, after advancing to next vertex

// next event estimation, first two for light (set) selection and cdf
#define DIM_LIGHT_SEL_1 0
#define DIM_LIGHT_SEL_2 1
#define DIM_POSITION_X  2
#define DIM_POSITION_Y  3
#define DIM_LIGHT_END   4

#ifdef USE_SIMPLIFIED_CAMERA
#undef DIM_CAMERA_END
#define DIM_CAMERA_END DIM_SIMPLE_CAMERA_END
#endif

#endif
