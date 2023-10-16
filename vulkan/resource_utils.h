// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

// External includes
#include <vulkan/vulkan.h>
#include <vector>

// Internal includes
#include "vulkan_utils.h"
#include "../librender/material.h"
#include "image.h"

void create_vulkan_textures_from_images(vkrt::CommandStream *async_commands, 
                                        const std::vector<Image> &imageArray,
                                        std::vector<vkrt::Texture2D>& textureArray,
                                        vkrt::MemorySource& static_memory_arena,
                                        vkrt::MemorySource& scratch_memory_arena);
