// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>

struct CameraDesc {
    glm::vec3 position, center, up;
    float fov_y;
};
