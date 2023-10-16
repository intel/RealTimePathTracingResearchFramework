// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef COLOR_MATCHING_GLSL
#define COLOR_MATCHING_GLSL

#ifndef CM_CIE_MIN
    #define CM_CIE_MIN           360.f
#endif
#ifndef CM_CIE_MAX
    #define CM_CIE_MAX           830.f
#endif
#ifndef CM_CIE_SAMPLES
    #define CM_CIE_SAMPLES       95
#endif

#ifndef CM_TABLE_X
    #define CM_TABLE_X (&cie1931_tbl[0])
#endif
#ifndef CM_TABLE_Y
    #define CM_TABLE_Y (&cie1931_tbl[CM_CIE_SAMPLES])
#endif
#ifndef CM_TABLE_Z
    #define CM_TABLE_Z (&cie1931_tbl[2 * CM_CIE_SAMPLES])
#endif

inline vec3 cie1931_xyz(float wavelength) {
    float t = (wavelength - CM_CIE_MIN) / (CM_CIE_MAX - CM_CIE_MIN);
    float fi = t * float(CM_CIE_SAMPLES - 1);
    int i = clamp(int(fi), 0, CM_CIE_SAMPLES - 2);

    vec3 x = vec3(CM_TABLE_X[i], CM_TABLE_Y[i], CM_TABLE_Z[i]);
    vec3 y = vec3(CM_TABLE_X[i+1], CM_TABLE_Y[i+1], CM_TABLE_Z[i+1]);

    return mix(x, y, fi - float(i));
}

// Source: https://github.com/mitsuba-renderer/mitsuba2/blob/ab5a49d4199a1b08d4d6557dfe6b0336fff4cfdf/include/mitsuba/core/spectrum.h#L219
// subject to the following license:

// Copyright (c) 2017 Wenzel Jakob <wenzel.jakob@epfl.ch>, All rights reserved.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:

// 1. Redistributions of source code must retain the above copyright notice, this
//    list of conditions and the following disclaimer.

// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.

// 3. Neither the name of the copyright holder nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// You are under no obligation whatsoever to provide any bug fixes, patches, or
// upgrades to the features, functionality or performance of the source code
// ("Enhancements") to anyone; however, if you choose to make your Enhancements
// available either publicly, or directly to the author of this software, without
// imposing a separate written license agreement for such Enhancements, then you
// hereby grant the following license: a non-exclusive, royalty-free perpetual
// license to install, use, modify, prepare derivative works, incorporate into
// other computer software, distribute, and sublicense such enhancements or
// derivative works thereof, in binary and source code form.

// Convert ITU-R Rec. BT.709 linear RGB to XYZ tristimulus values
inline vec3 srgb_to_xyz(vec3 rgb) {
    const mat3 M = transpose(mat3(0.412453f, 0.357580f, 0.180423f,
                                  0.212671f, 0.715160f, 0.072169f,
                                  0.019334f, 0.119193f, 0.950227f));
    return M * rgb;
}

// Convert XYZ tristimulus values to ITU-R Rec. BT.709 linear RGB
inline vec3 xyz_to_srgb(vec3 xyz) {
    const mat3 M = transpose(mat3(3.240479f, -1.537150f, -0.498535f,
                                 -0.969256f,  1.875991f,  0.041556f,
                                  0.055648f, -0.204043f,  1.057311f));
    return M * xyz;
}

#endif
