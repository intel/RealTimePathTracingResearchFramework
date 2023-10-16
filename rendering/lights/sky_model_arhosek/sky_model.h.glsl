// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef ARHOSEK_SKY_MODEL_PARAMS
#define ARHOSEK_SKY_MODEL_PARAMS

struct SkyModelParams {
    GLM(vec4) configs[9];
    GLM(vec4) radiances;
};

#endif
