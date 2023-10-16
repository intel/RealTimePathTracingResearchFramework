// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef RENDERING_PROCESSING_REPROJECTION_H
#define RENDERING_PROCESSING_REPROJECTION_H

/*
 * NOTE: If you add to the defines please keep the list of strings below
 *       in sync!
 */
#define REPROJECTION_MODE_NONE 0
#define REPROJECTION_MODE_DISCARD_HISTORY 1
#define REPROJECTION_MODE_ACCUMULATE 2

#define REPROJECTION_MODE_NAMES \
    "NONE", \
    "DISCARD_HISTORY", \
    "ACCUMULATE"

#endif /* RENDERING_PROCESSING_REPROJECTION_H */
