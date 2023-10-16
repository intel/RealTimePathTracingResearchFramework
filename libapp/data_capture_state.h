// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>
#include "imgui.h"
#include "imstate.h"
#include "imutils.h"

#include <memory>
#include "librender/raytrace_backend.h"
#include "librender/render_backend.h"

#include "libdatacapture/pois.h"
#include "libdatacapture/viewpoints.h"

struct DataCaptureTools {
    rt_datacapture::RandomSampler capture_rng;
    RaytraceBackend* raytracer = nullptr;

    // ownership of separate ray tracer
    std::unique_ptr<RaytraceBackend> aux_raytracer;
    
    DataCaptureTools(RenderBackend* renderer) {
       raytracer = dynamic_cast<RaytraceBackend*>(renderer);
       if (!raytracer) {
            assert(false);
       }
    }

    void set_scene(Scene const& scene) {
        if (aux_raytracer)
            aux_raytracer->set_scene(scene);
    }
};

struct DataCaptureState {
    std::vector<glm::vec3> poi_perspectives = { };
    int num_pois_per_perspective = 1000;
    std::vector<rt_datacapture::Poi> pois;

    void poi_state(DataCaptureTools& capture_tools, glm::vec3 camera_pos) {
        IMGUI_STATE(ImGui::SliderInt, "pois/source", &num_pois_per_perspective, 1, 100000);
        IMGUI_VOLATILE(ImGui::AlignTextToFramePadding());
        IMGUI_VOLATILE(ImGui::Text("Active POIs: %d", (int) pois.size()));
        IMGUI_VOLATILE(ImGui::SameLine());
        if (IMGUI_STATE_ACTION(ImGui::Button, "Regenerate POIs")) {
            pois.clear();
            pois.resize(num_pois_per_perspective * poi_perspectives.size());
            int poi_cursor = 0;
            for (auto& poi_source : poi_perspectives) {
                rt_datacapture::collect_visible_points(*capture_tools.raytracer, poi_source, pois.data() + poi_cursor, num_pois_per_perspective);
                poi_cursor += num_pois_per_perspective;
            }
            poi_cursor = rt_datacapture::prune_pois(*capture_tools.raytracer, pois.data(), (int) pois.size(), capture_tools.capture_rng);
            pois.resize(poi_cursor);
        }
        bool add_perspective = false;
        int fill_count = -1;
        if (IMGUI_LIST_BEGIN(ui_begin_list_frame, "poi sources", &poi_perspectives, &fill_count, &add_perspective)) {
            if (fill_count >= 0) {
                poi_perspectives.clear();
                poi_perspectives.resize(fill_count);
            }
            if (add_perspective)
                poi_perspectives.push_back(camera_pos);
            for (auto& poi_source : poi_perspectives) {
                bool keep = true, up = false, down = false;
                if (IMGUI_STATE_BEGIN(ui_list_node_ex, "##TEST", &poi_source, ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen, &keep, &up, &down)) {
                    IMGUI_STATE3(ImGui::InputFloat, "position", &poi_source.x);
                    IMGUI_STATE_END(ImGui::TreePop, &poi_source);
                }
            }
        }
        IMGUI_LIST_END(ui_end_list_frame, &poi_perspectives);
    }
    
    int _datacapture_anchor = 0;
    void state(DataCaptureTools& capture_tools, glm::vec3 camera_pos) {
        if (IMGUI_STATE_BEGIN_ALWAYS(ImGui::Begin, "data capture", &_datacapture_anchor)) {
            poi_state(capture_tools, camera_pos);
        }
        IMGUI_STATE_END(ImGui::End, &_datacapture_anchor);
    }
};
