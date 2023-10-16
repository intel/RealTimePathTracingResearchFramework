// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include <glm/glm.hpp>

#ifndef GLM
    #define GLM(type) glm::type
#endif
#include "render_params.glsl.h"
#include "../rendering/lights/tri.h.glsl"
#include "../rendering/lights/quad.h.glsl"
#include "../rendering/lights/point.h.glsl"
#include "../rendering/lights/light.h.glsl"

struct Scene;
struct ParameterizedMesh;
struct Mesh;
struct BaseMaterial;

std::vector<TriLight> collect_emitters(Scene const& scene);
std::vector<TriLight> collect_emitters(glm::mat4 const& transform, ParameterizedMesh const& pm, Mesh const& mesh, std::vector<BaseMaterial> const& materials);

// importance sampling tools
struct BinnedLightSampling {
    std::vector<TriLight> emitters;
    std::vector<float> radiances;
    LightSamplingConfig params;
    BinnedLightSampling() {
        params.bin_size = 0; // mark uninitialized
    }
    int bin_count() const;
};
void update_light_sampling(BinnedLightSampling& binned, std::vector<TriLight> const& emitters, LightSamplingConfig params);

struct LightSamplingSetup {
    std::vector<TriLight> emitters;
    BinnedLightSampling binned;
};

// compute representative radiance value based on closest shading points to light source where variance is still visibly perceived (depends on viewer scale)
std::vector<float> estimate_normalized_radiance(Scene const* scene, std::vector<TriLight> const& emitters, float min_perceived_receiver_dist);
// remove short-range emitters that contribute no noticeable light outside their local environment (depends on camera exposure)
void trim_dim_emitters(std::vector<TriLight>& emitters, std::vector<float> &radiances, float min_radiance);
// partition emitters into approx. equal-weight bins for importance sampling
void equalize_emitter_bins(std::vector<TriLight>& emitters, std::vector<float> &radiances, int bin_size);
