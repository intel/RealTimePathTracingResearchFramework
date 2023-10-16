// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <glm/glm.hpp>

namespace shaders_default {

using namespace glm;
#include "../language.hpp"

#define MAKE_RANDOM_TABLE(TYPE, NAME) TYPE NAME;
#include "../pointsets/sobol.glsl"
#include "../pointsets/lcg_rng.glsl"

#include "../util.glsl"

#include "../bsdfs/base_material.h.glsl"

#define NO_MATERIAL_REGISTRATION
namespace glft_bsdf {
    #include "../bsdfs/gltf_bsdf.glsl"
}

#include "../lights/tri.glsl"

uint32_t const global_num_lights = 10;
TriLightData lights[global_num_lights] = {};

// assemble light transport algorithm
#define SCENE_GET_LIGHT_SOURCE(light_id) decode_tri_light(lights[light_id])
#define SCENE_GET_LIGHT_SOURCE_COUNT()   int(global_num_lights)

namespace linear_light_selection {

#include "../mc/lights_linear.glsl"

}

bool raytrace_test_visibility(const vec3 from, const vec3 dir, float dist) { return true; }

} // namespace
