// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

// #include "../../librender/render_params.glsl.h"

#if RBO_rng_variant == RNG_VARIANT_BN
#include "bn_rng.glsl"
#elif RBO_rng_variant == RNG_VARIANT_SOBOL
#include "sobol.glsl"
#elif RBO_rng_variant == RNG_VARIANT_Z_SBL
#define Z_ORDER_SHUFFLING
#include "sobol.glsl"
#endif
