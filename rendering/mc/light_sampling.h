// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RENDERING_LIGHT_SAMPLING_H
#define RENDERING_LIGHT_SAMPLING_H

/*
 * NOTE: If you add to the defines please keep the list of strings below
 *       in sync!
 */
#define LIGHT_SAMPLING_VARIANT_NONE 0
#define LIGHT_SAMPLING_VARIANT_RIS 1

#define LIGHT_SAMPLING_VARIANT_NAMES \
    "NONE", \
    "RIS"

// note: the default value will be omitted from build command lines
#define RBO_light_sampling_variant_DEFAULT LIGHT_SAMPLING_VARIANT_RIS
#define RBO_light_sampling_bucket_count_DEFAULT 16

// ensure consistent defaults when option is omitted
#ifndef RBO_light_sampling_variant
#define RBO_light_sampling_variant RBO_light_sampling_variant_DEFAULT
#endif

#define RBO_light_sampling_variant_NAMES_PREFIX "LIGHT_SAMPLING_VARIANT_"
#define RBO_light_sampling_variant_NAMES LIGHT_SAMPLING_VARIANT_NAMES

#endif
