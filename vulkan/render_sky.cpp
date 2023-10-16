// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_vulkan.h"

#include "types.h"
#include "util.h"
#include "profiling.h"

#include <algorithm>
#include <numeric>

#include "../rendering/lights/sky_model_arhosek/sky_model.h"

namespace glsl {
    using namespace glm;
    #include "../rendering/language.hpp"

    #include "gpu_params.glsl"

    #include "../rendering/color/color_matching.h"
    #include "../rendering/color/color_matching.glsl"
}

void RenderVulkan::update_sky_light(SceneConfig const& config) {
    glm::vec3 sun_dir = glm::normalize(config.sun_dir);

    ArHosekSkyModelState state;
    arhosek_rgb_skymodelstate_alloc_init(config.turbidity, dot(config.albedo, glm::vec3(0.3333f)), sun_dir.y, &state);

    glsl::SceneParams& sceneParams = global_params(true)->scene_params;
    sceneParams.sun_dir = sun_dir;
    sceneParams.sun_cos_angle = std::cos(glm::radians(0.53f) / 2.0f);

    glsl::SkyModelParams& skyParams = sceneParams.sky_params;
    for (int i = 0; i < 9; ++i)
        skyParams.configs[i] = glm::vec4(state.configs[0][i], state.configs[1][i], state.configs[2][i], 0.0f);
    skyParams.radiances = glm::vec4(state.radiances[0], state.radiances[1], state.radiances[2], 0.0f);

    // update sun
    {
        ArHosekSkyModelState sunState;
        arhosekskymodelstate_alloc_init(state.elevation, state.turbidity, state.albedo, &sunState);
        glm::vec3 xyz_radiance(0.0f);
        int numSamples = 0;
        float last_wavelength = CM_CIE_MIN;
        for (int i = 0; i < CM_CIE_SAMPLES; ++i) {
            float wavelength = float(i) * float(CM_CIE_MAX - CM_CIE_MIN) / float(CM_CIE_SAMPLES - 1) + float(CM_CIE_MIN);
            if (wavelength > 720.0f)
                break; // higher wavelengths not supported by the sky model
            float radiance = arhosekskymodel_solar_radiance(&sunState, sun_dir.y, 0.0, wavelength);
            // radiace by default includes scattering
            radiance -= arhosekskymodel_radiance(&sunState, sun_dir.y, 0.0, wavelength);
            {
                using namespace glsl;
                xyz_radiance += glm::vec3(CM_TABLE_X[i], CM_TABLE_Y[i], CM_TABLE_Z[i]) * radiance;
                ++numSamples;
                last_wavelength = wavelength;
            }
        }
        xyz_radiance *= float(last_wavelength - CM_CIE_MIN) / float(numSamples);
        if (sun_dir.y > 0.0f && all(greaterThanEqual(xyz_radiance, glm::vec3(0.0f))))
            sceneParams.sun_radiance = glm::vec4(0.01f * glsl::xyz_to_srgb(xyz_radiance), 1.0f);
        else
            sceneParams.sun_radiance = glm::vec4(0.0f);

        if (sceneParams.light_sampling.light_count > 0)
            sceneParams.sun_radiance.w *= 0.5f;
        else
            sceneParams.sun_radiance.w = 1.0f;
    }
}
