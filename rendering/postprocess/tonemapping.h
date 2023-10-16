// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef TONEMAPPING_H_GLSL
#define TONEMAPPING_H_GLSL

// Tonemapping modes
#define NO_TONE_MAPPING 0
#define NEUTRAL_TONE_MAPPING 1
#define FAST_TONE_MAPPING 2

// note: order has to match numbers!
#define TONEMAPPING_MODES_NAMES \
    "NO_TONE_MAPPING", \
    "NEUTRAL_TONE_MAPPING", \
    "FAST_TONE_MAPPING"

// remap old operator names
#define COMPATIBILITY_TONEMAPPING_OPERATOR_NAMES \
    "TONEMAPPING_OPERATOR_LINEAR", \
    "TONEMAPPING_OPERATOR_LOG"


#endif // TONEMAPPING_H_GLSL

