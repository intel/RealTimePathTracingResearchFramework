// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>

#ifndef GLM
    #define GLM(type) glm::type
#endif
#ifndef GLCPP_DEFAULT
    #define GLCPP_DEFAULT(...) __VA_ARGS__
#endif

#define DEFAULT_GEOMETRY_BUFFER_TYPES
#include "../rendering/rt/geometry.h.glsl"
