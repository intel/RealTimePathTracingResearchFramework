// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef ARHOSEK_SKY_MODEL
#define ARHOSEK_SKY_MODEL

#include "sky_model.h.glsl"

/*
This source is published under the following 3-clause BSD license.

Adapted from: "An Analytic Model for Full Spectral Sky-Dome Radiance"
Copyright (c) 2012 - 2013, Lukas Hosek and Alexander Wilkie
All rights reserved.

Redistribution and use in source and binary forms, with or without 
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * None of the names of the contributors may be used to endorse or promote 
      products derived from this software without specific prior written 
      permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

vec3 skymodel_radiance(
    const SkyModelParams    state,
    const vec3              sun_dir,
    const vec3              view_dir
    )
{
    float cosTheta = clamp(view_dir.y, 0.0f, 1.0f);
    float cosGamma = clamp(dot(view_dir, sun_dir), -1.0f, 1.0f);
    float gamma = acos(cosTheta);

    const vec3 expM = exp(vec3(state.configs[4]) * gamma);
    const float rayM = cosGamma*cosGamma;
    const vec3 mieM = (1.0f + cosGamma * cosGamma) / pow(1.0f + vec3(state.configs[8]*state.configs[8]) - 2.0f * vec3(state.configs[8]) * cosGamma, vec3(1.5f));
    const float zenith = sqrt(cosTheta);

    const vec3 radiance_coeffs = (1.0 + vec3(state.configs[0]) * exp(vec3(state.configs[1]) / (cosTheta + 0.01))) *
            (vec3(state.configs[2]) + vec3(state.configs[3]) * expM + vec3(state.configs[5]) * rayM + vec3(state.configs[6]) * mieM + vec3(state.configs[7]) * zenith);

    return radiance_coeffs * vec3(state.radiances) * 0.01f;
}

#endif
