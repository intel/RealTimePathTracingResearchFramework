// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef BASE_MATERIAL_PARAMS_HEADER
#define BASE_MATERIAL_PARAMS_HEADER

#define BASE_MATERIAL_NOALPHA 0x01
#define BASE_MATERIAL_ONESIDED 0x02
#define BASE_MATERIAL_VOLUME 0x04
#define BASE_MATERIAL_EXTENDED 0x08 // enable specular transmission (todo: any others optional?)
#define BASE_MATERIAL_NEURAL 0x10

struct BaseMaterial {
    GLM(vec3) base_color GLCPP_DEFAULT(= GLM(vec3)(0.9f));
    int32_t normal_map GLCPP_DEFAULT(= -1);
    
    uint32_t flags GLCPP_DEFAULT(= 0);
    float roughness GLCPP_DEFAULT(= 1);
    float specular GLCPP_DEFAULT(= 0.5f);
    float metallic GLCPP_DEFAULT(= 0);

    float sheen GLCPP_DEFAULT(= 0);
    float sheen_tint GLCPP_DEFAULT(= 0);
    float clearcoat GLCPP_DEFAULT(= 0);
    float clearcoat_gloss GLCPP_DEFAULT(= 0.1f);

    float ior GLCPP_DEFAULT(= 1.5);
    float specular_transmission GLCPP_DEFAULT(= 0);
    float anisotropy GLCPP_DEFAULT(= 0);
    float specular_tint GLCPP_DEFAULT(= 0);

    GLM(vec3) transmission_color GLCPP_DEFAULT(= GLM(vec3)(1.0f));
    float emission_intensity GLCPP_DEFAULT(= 0.0f);
};

#ifdef UNROLL_STANDARD_TEXTURES
#define STANDARD_TEXTURE_COUNT 3
#define STANDARD_TEXTURE_BASECOLOR_SLOT 1
#define STANDARD_TEXTURE_NORMAL_SLOT 0
#define STANDARD_TEXTURE_SPECULAR_SLOT 2
#endif

#endif
