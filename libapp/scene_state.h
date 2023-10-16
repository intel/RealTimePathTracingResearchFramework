// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

// Builtin includes
#include <algorithm>
#include <string>

// External libraries
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"
#include "imstate.h"
#include "imutils.h"
#include "shell.h"

// RPTR includes
#include "librender/render_backend.h"
#include "types.h"
#include "../rendering/postprocess/tonemapping.h"

struct Scene;

struct BasicSceneState {
    SceneConfig scene_config;
    bool scene_changed = true;
    bool renderer_changed = false;

    bool state(RenderBackend* renderer, std::vector<RenderExtension*>& extensions) {

        // each extension renders it's UI in a different window
        int numExtensions = int_cast(extensions.size());
        for (int extensionIdx = 0; extensionIdx < numExtensions; ++extensionIdx) {
            RenderExtension *currentExtension = shell.renderer_extensions[extensionIdx];
            scene_changed |= currentExtension->ui_and_state(renderer_changed);
        }

        if (!IMGUI_VOLATILE_HEADER(ImGui::Begin, "Scene")) {
          IMGUI_VOLATILE(ImGui::End());
          return false;
        }

        renderer_changed = false;
        if (IMGUI_STATE_BEGIN_HEADER(ImGui::CollapsingHeader, "Sensor", &renderer->params, ImGuiTreeNodeFlags_DefaultOpen)) {
            renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "aperture radius", &renderer->params.aperture_radius, 0.00f, 5.0f);
            renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "focal distance", &renderer->params.focus_distance, 0.5f, 50.0f);
            renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "focal length", &renderer->params.focal_length, 16.0f, 150.0f);

            renderer_changed |= IMGUI_STATE(ImGui::SliderInt, "light bin size", &renderer->lighting_params.bin_size, 1, 32);
            renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "light mis angle", &renderer->lighting_params.light_mis_angle, 0.0f, 20.0f);

            IMGUI_STATE_END_HEADER(&renderer->params);
        }

        if (IMGUI_STATE_BEGIN_HEADER(ImGui::CollapsingHeader, "Tonemapping", &renderer->params, ImGuiTreeNodeFlags_DefaultOpen)) {
            static const char* const tonemapping_operators[] = {
                COMPATIBILITY_TONEMAPPING_OPERATOR_NAMES
            };
            int &op = renderer->params.early_tone_mapping_mode;
            const int last_active = std::max(std::min(op, array_ilen(tonemapping_operators)-1), 0);
            if (IMGUI_STATE_BEGIN_ATOMIC_COMBO(ImGui::BeginCombo, "operator", tonemapping_operators, tonemapping_operators[last_active])) {
                for (int i = 0; i < array_ilen(tonemapping_operators); ++i) {
                    if (IMGUI_STATE(ImGui::Selectable, tonemapping_operators[i], i == last_active)) {
                        op = i;
                        renderer_changed |= true;
                    }
                }
                IMGUI_STATE_END(ImGui::EndCombo, tonemapping_operators);
            }

            renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "exposure", &renderer->params.exposure, -15.0f, 15.0f);
            IMGUI_STATE_END_HEADER(&renderer->params);
        }

        if (IMGUI_STATE_BEGIN_HEADER(ImGui::CollapsingHeader, "Sun", &scene_config.sun_dir, ImGuiTreeNodeFlags_DefaultOpen)) {
            bool sun_changed = false;

            bool sun_dir_changed = false;
            float sun_theta = 90.0f - glm::degrees( acos(scene_config.sun_dir.y) );
            float sun_phi = glm::degrees( atan2(scene_config.sun_dir.z, scene_config.sun_dir.x) );
            sun_dir_changed |= IMGUI_STATE(ImGui::SliderFloat, "height", &sun_theta, 0.0f, 90.0f);
            sun_dir_changed |= IMGUI_STATE(ImGui::SliderFloat, "angle", &sun_phi, -180.0f, 180.0f);
            if (sun_dir_changed) {
                float cosTheta = cos(glm::radians(90.0f - sun_theta));
                float sinTheta = sin(glm::radians(90.0f - sun_theta));
                scene_config.sun_dir = glm::vec3( cos(glm::radians(sun_phi)) * sinTheta, cosTheta, sin(glm::radians(sun_phi)) * sinTheta );
                sun_changed = true;
            }

            sun_changed |= IMGUI_STATE(ImGui::SliderFloat, "turbidity", &scene_config.turbidity, 1.0f, 10.0f);
            sun_changed |= IMGUI_STATE(ImGui::ColorEdit3, "Color", glm::value_ptr(scene_config.albedo));

            scene_changed |= sun_changed;

            IMGUI_STATE_END_HEADER(&scene_config.sun_dir);
        }

        if (IMGUI_STATE_BEGIN_HEADER(ImGui::CollapsingHeader, "Scene", &scene_config.bump_scale, ImGuiTreeNodeFlags_DefaultOpen)) {
            scene_changed |= IMGUI_STATE(ImGui::SliderFloat, "bump scale", &scene_config.bump_scale, 0.5f, 10.0f);

            IMGUI_STATE_END_HEADER(&scene_config.bump_scale);
        }


        if (scene_changed)
            renderer->update_config(scene_config);

        IMGUI_VOLATILE(ImGui::End());

        // by convenction, trigger scene update on render update
        scene_changed |= renderer_changed;

        return scene_changed;
    };

    static std::string make_scene_id(const std::string &scene_name) {
      std::string scene_id = scene_name;
      std::replace(scene_id.begin(), scene_id.end(), '\\', '/');
        int delims = 0;
        for (char const* c = scene_id.c_str() + scene_id.size(); c-- != scene_id.c_str(); ) {
            if (*c == '/') {
                if (delims == 1) {
                    scene_id.erase(scene_id.begin(), scene_id.begin() + (c - scene_id.c_str() + 1));
                    break;
                }
                ++delims;
            }
        }
      scene_id.insert(0, "Scene##");
      return scene_id;
    }

    static std::string get_scene_info(std::vector<std::string> const& scene_names,
            Scene const& scene);
};

struct SceneDescription {
    std::vector<std::string> scene_files;
    std::vector<std::string> ids; // = make_scene_id(scene_name);
    std::string info; // = get_scene_info(scene_name, scene)

    glm::vec3 center = glm::vec3(0.0f);
    float radius = 100.0f;

    SceneDescription() = default;
    SceneDescription(const std::vector<std::string> &scene_file, Scene const& scene);
};

void apply_selected_camera(Shell::DefaultArgs& args, Scene const& scene);

struct SceneLoaderParams;
void imstate_scene_loader_parameters(SceneLoaderParams& params, const std::vector<std::string> &fnames);
