// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#ifndef GLSL_LANGUAGE_ADAPTER
#define GLSL_LANGUAGE_ADAPTER

// qualifiers
#define GLSL_out(type) type&
#define GLSL_inout(type) type&
#define GLSL_in(type) type const&
#define GLSL_to_out_ptr(lvalue) &(lvalue)

#define GLSL_construct(...) { __VA_ARGS__  }
#define GLSL_unused(x) (void) x
#ifndef GLM
    #define GLM(type) glm::type
#endif
#ifndef GLCPP_DEFAULT
    #define GLCPP_DEFAULT(...) __VA_ARGS__
#endif

#define GLSL_UINT64 uint64_t

#define UNROLL_FOR for // todo: specialize for CUDA?
#define DYNAMIC_FOR for // todo: specialize for CUDA?

inline vec2 fma2(vec2 a, vec2 b, vec2 c) {
    return vec2(
          fma(a.x, b.x, c.x)
        , fma(a.y, b.y, c.y)
    );
}

inline vec3 fma3(vec3 a, vec3 b, vec3 c) {
    return vec3(
          fma(a.x, b.x, c.x)
        , fma(a.y, b.y, c.y)
        , fma(a.z, b.z, c.z)
    );
}

#endif
