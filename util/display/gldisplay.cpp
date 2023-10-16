// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "gldisplay.h"
#include "render_graphic.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <GLFW/glfw3.h>
#include "backends/imgui_impl_opengl3.h"
#include "imgui_backend.h"

const std::string fullscreen_quad_vs =
  "#version 330 core\n"
  "\n"
  "const vec4 pos[4] = vec4[4](\n"
  "	vec4(-1, 1, 0.5, 1),\n"
  "	vec4(-1, -1, 0.5, 1),\n"
  "	vec4(1, 1, 0.5, 1),\n"
  "	vec4(1, -1, 0.5, 1)\n"
  ");\n"
  "\n"
  "void main(void){\n"
  "	gl_Position = pos[gl_VertexID];\n"
  "}\n";

const std::string display_texture_fs =
  "#version 330 core\n"
  "\n"
  "uniform sampler2D img;\n"
  "\n"
  "out vec4 color;\n"
  "\n"
  "void main(void){\n"
  "	ivec2 uv = ivec2(gl_FragCoord.x, textureSize(img, 0).y - gl_FragCoord.y);\n"
  "	color = texelFetch(img, uv, 0);\n"
  "}\n";


Display* create_opengl_display(GLFWwindow *window, const char *device_override) {
    return new GLDisplay(window);
}

GLDisplay::GLDisplay(GLFWwindow *win)
    : window(win), render_texture(-1)
{
    glfwMakeContextCurrent(window);

    if (!gladLoadGL()) {
        throw std::runtime_error("Failed to initialize OpenGL");
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    display_render = std::make_unique<Shader>(fullscreen_quad_vs, display_texture_fs);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glDisable(GL_DEPTH_TEST);
}

GLDisplay::~GLDisplay()
{
    glDeleteVertexArrays(1, &vao);
    if (render_texture != GLuint(-1)) {
        glDeleteTextures(1, &render_texture);
    }
    ImGui_ImplOpenGL3_Shutdown();
}

std::string GLDisplay::gpu_brand() const
{
    return reinterpret_cast<const char *>(glGetString(GL_RENDERER));
}

std::string GLDisplay::name() const
{
    return "OpenGL";
}

void GLDisplay::resize(const int fb_width, const int fb_height)
{
    if (render_texture != GLuint(-1)) {
        glDeleteTextures(1, &render_texture);
    }
    this->fb_dims = glm::ivec2(fb_width, fb_height);
    glGenTextures(1, &render_texture);
    glBindTexture(GL_TEXTURE_2D, render_texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 fb_dims.x,
                 fb_dims.y,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void GLDisplay::init_ui_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
}

void GLDisplay::new_frame()
{
}

void GLDisplay::display(const std::vector<uint32_t> &img)
{
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, render_texture);
    glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, fb_dims.x, fb_dims.y, GL_RGBA, GL_UNSIGNED_BYTE, img.data());

    display_native(render_texture);
}

void GLDisplay::display_native(const GLuint img)
{
    glViewport(0, 0, fb_dims.x, fb_dims.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(display_render->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, img);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
}

void Display::display(RenderGraphic *renderer) {
    const glm::uvec3 fbSize = renderer->get_framebuffer_size();
    assert(fbSize.z == sizeof(uint32_t));
    framebuffer.resize(fbSize.x * static_cast<size_t>(fbSize.y));
    bool available = framebuffer.size() == renderer->readback_framebuffer(
        framebuffer.size() * sizeof(uint32_t),
        reinterpret_cast<unsigned char*>(framebuffer.data()));
    assert(available);
    (void) available;
    return display(framebuffer);
}

void GLDisplay::display(RenderGraphic *renderer) {
    if (auto* render_gl = dynamic_cast<RenderGLGraphic*>(renderer))
        return display_native(render_gl->display_texture);
    else
        return Display::display(renderer);
}
