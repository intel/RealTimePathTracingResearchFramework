// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "imgui.h"
#include "imstate.h"
#include "shell.h"
#include "interactive_camera.h"

inline bool camera_xi(OrientedCamera& camera) {
    if (!IMGUI_VOLATILE_HEADER(ImGui::Begin, "Scene")) {
        IMGUI_VOLATILE(ImGui::End());
        return false;
    }

    bool camera_changed = false;
    if (IMGUI_STATE_BEGIN_HEADER(ImGui::CollapsingHeader, "Camera", &camera, ImGuiTreeNodeFlags_DefaultOpen)) {
        IMGUI_STATE(ImGui::SliderFloat, "speed", &camera.speed, 0.0001f, 100.0f);
        IMGUI_STATE(ImGui::SliderFloat, "sensitivity", &camera.sensitivity, 0.01f, 10.0f);
        glm::vec3 pos = camera.eye();
        if (IMGUI_STATE3(ImGui::InputFloat, "position", glm::value_ptr(pos))) {
            camera.set_position(pos);
            camera_changed = true;
        }
        glm::vec3 dir = camera.dir();
        glm::vec3 up = camera.up();
        bool dir_changed = IMGUI_STATE3(ImGui::InputFloat, "direction", glm::value_ptr(dir));
        bool up_changed = IMGUI_STATE3(ImGui::InputFloat, "up", glm::value_ptr(up));
        if (up_changed) { // may include dir_changed!
            camera.set_direction(dir, up);
            camera_changed = true;
        }
        else if (dir_changed) { // only direction changed
            camera.set_direction(dir);
            camera_changed = true;
        }

        IMGUI_STATE_END_HEADER(&camera);
    }

    IMGUI_VOLATILE(ImGui::End());
    return camera_changed;
}

inline bool default_camera_movement(OrientedCamera& camera, Shell& shell, ImGuiIO& io, Shell::DefaultArgs const& config_args) {
    bool camera_changed = false;
    if (!io.WantCaptureMouse) {
        if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f) {
            const glm::vec2 cur_mouse = shell.transform_mouse(glm::vec2(io.MousePos.x, io.MousePos.y));
            const glm::vec2 prev_mouse = shell.transform_mouse(glm::vec2(io.MousePos.x, io.MousePos.y) - glm::vec2(io.MouseDelta.x, io.MouseDelta.y));
            if (io.MouseDown[0]) {
                camera.rotate(prev_mouse, cur_mouse);
                camera_changed = true;
            } else if (io.MouseDown[1]) {
                camera.pan(cur_mouse - prev_mouse);
                camera_changed = true;
            }
        }
        if (io.MouseWheel != 0.0f) {
            camera.zoom(io.MouseWheel * 0.1f);
            camera_changed = true;
        }
    }
    if (!io.WantCaptureKeyboard) {
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            camera.move_local(glm::vec3(0, 0, 1), io.DeltaTime);
            camera_changed = true;
        } if (ImGui::IsKeyDown(ImGuiKey_W)) {
            camera.move_local(glm::vec3(0, 0, -1), io.DeltaTime);
            camera_changed = true;
        } if (ImGui::IsKeyDown(ImGuiKey_D)) {
            camera.move_local(glm::vec3(1, 0, 0), io.DeltaTime);
            camera_changed = true;
        } if (ImGui::IsKeyDown(ImGuiKey_A)) {
            camera.move_local(glm::vec3(-1, 0, 0), io.DeltaTime);
            camera_changed = true;
        } if (ImGui::IsKeyDown(ImGuiKey_Space)) {
            camera.move_local(glm::vec3(0, 1, 0), io.DeltaTime);
            camera_changed = true;
        } if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            camera.move_local(glm::vec3(0, -1, 0), io.DeltaTime);
            camera_changed = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_P)) {
            auto eye = camera.eye();
            auto center = camera.center();
            auto up = camera.up();
            std::cout << "-eye " << eye.x << " " << eye.y << " " << eye.z
                        << " -center " << center.x << " " << center.y << " " << center.z
                        << " -up " << up.x << " " << up.y << " " << up.z << " -fov "
                        << config_args.fov_y << "\n";
        } 
    }
    return camera_changed;
}
