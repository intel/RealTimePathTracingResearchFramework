// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "file_mapping.h"

#ifndef GLM
    #define GLM(type) glm::type
#endif
#ifndef GLCPP_DEFAULT
    #define GLCPP_DEFAULT(...) __VA_ARGS__
#endif

#include "../rendering/bsdfs/texture_channel_mask.h"
#include "../rendering/bsdfs/base_material.h.glsl"

