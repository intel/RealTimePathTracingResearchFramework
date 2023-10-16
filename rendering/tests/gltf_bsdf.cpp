// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <glm/glm.hpp>

namespace shaders_gltf {

using namespace glm;

#include "../language.hpp"
#include "../util.glsl"
#include "../bsdfs/base_material.h.glsl"
#include "../bsdfs/gltf_bsdf.glsl"

}

#include <random>

static float next_randf() {
    return (float) rand() / (float) RAND_MAX;
}

int test_sample(bool transmission, bool metal) {
    using namespace shaders_gltf;

    GLTFMaterial material = { };
    material.base_color = glm::vec3(0.5f);
    material.metallic = float(metal);
    material.specular = 0.2f;
    material.roughness = 0.1f;
    material.ior = 1.5f;
    material.specular_transmission = float(transmission);
    material.transmission_color = glm::vec3(1.0f);

    int numTests = 100000000;
    int numFailed = 0;
    for (int i = 0; i < numTests; ++i) {
        glm::vec3 n = normalize( glm::vec3(next_randf(), next_randf(), next_randf()) * 2.0f - glm::vec3(1.0f) );
        glm::vec3 wo = normalize( glm::vec3(next_randf(), next_randf(), next_randf()) * 2.0f - glm::vec3(1.0f) );
        glm::vec3 wi; // = normalize(glm::vec3(next_randf(), next_randf(), next_randf()));

        if (dot(n, wo) < 0.0f)
            wo = -wo;

        glm::vec3 v_x, v_y;
        ortho_basis(v_x, v_y, n);

        vec2 sample1 = glm::vec2(next_randf(), next_randf());
        vec2 sample2 = glm::vec2(next_randf(), next_randf());

        bool retry = false;
        do {
            float pdf, mis_pdf;
            vec3 value = sample_gltf_brdf(material, n, wo, wi, pdf, mis_pdf
                , sample1
                , sample2
                , v_x, v_y
                );

            if (pdf == 0.0f)
                continue;

            if (!all(lessThan(value, glm::vec3(2.0f)))) {
                if (!all(lessThan(value, glm::vec3(20.0f))))
                    printf("%f %f %f for %f at %f with pdf %f\n", value.x, value.y, value.z, dot(wi, n), dot(wo, n), pdf);
                ++numFailed;
            }
            if (!all(equal(value, value)) || !(pdf == pdf) || !(mis_pdf == mis_pdf))
                return 1;
        } while (retry);
    }

    printf("FAILED (%f%% success)\n", (1.0f - float(numFailed) / float(numTests)) * 100.0f);
    return 0;
}

int main() {
    printf("Testing reflection\n");
    test_sample(false, false);
    return 0;
}
