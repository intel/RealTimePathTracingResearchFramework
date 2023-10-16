// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include "unordered_vector.hpp"
#include "glad/glad.h"

struct Shader {
    GLuint program;
    unordered_vector<std::string, GLint> uniforms;

    Shader(const std::string &vert_src, const std::string &frag_src);
    Shader(const Shader &) = delete;
    Shader& operator=(const Shader &) = delete;
    ~Shader();
    template <typename T>
    void uniform(const std::string &unif, const T &t);

private:
    // Parse the uniform variable declarations in the src file and
    // add them to the uniforms map
    void parse_uniforms(const std::string &src);
};
