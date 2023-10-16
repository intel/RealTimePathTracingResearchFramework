// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <vector>
#include "display.h"
#include "glad/glad.h"
#include "shader.h"
#include <glm/glm.hpp>

struct GLFWwindow;

struct GLDisplay : Display {
    GLFWwindow *window;
    GLuint render_texture;
    GLuint vao;
    std::unique_ptr<Shader> display_render;

    GLDisplay(GLFWwindow *window);

    ~GLDisplay() override;

    std::string gpu_brand() const override;

    std::string name() const override;

    void resize(const int fb_width, const int fb_height) override;

    void init_ui_frame() override;
    void new_frame() override;

    void display(const std::vector<uint32_t> &img) override;
    void display_native(const GLuint img);
    void display(RenderGraphic *renderer) override;
};
