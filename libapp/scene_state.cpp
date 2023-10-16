// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "scene_state.h"
#include "librender/scene.h"
#include <sstream>
#include "util/util.h"
#include "imgui.h"

std::string BasicSceneState::get_scene_info(std::vector<std::string> const& scene_names, Scene const& scene) {
    std::stringstream ss;
    ss << "Scene\n";
    for (const auto &n: scene_names)
        ss << "'" << n << "'\n";
    ss  << "# Unique Triangles: " << pretty_print_count(scene.unique_tris())
        << " (animated: " << pretty_print_count(scene.unique_tris(Mesh::Dynamic | Mesh::SubtlyDynamic)) << ")\n"
        << "# Total Triangles: " << pretty_print_count(scene.total_tris())
        << " (animated: " << pretty_print_count(scene.total_tris(Mesh::Dynamic | Mesh::SubtlyDynamic)) << ")\n"
        << "# Geometries: " << scene.num_geometries() << "\n"
        << "# Meshes: " << scene.meshes.size() << "\n"
        << "# Parameterized Meshes: " << scene.parameterized_meshes.size() << "\n"
        << "# Instances: " << scene.instances.size() << "\n"
        << "# LoD groups: " << scene.lod_groups.size() << "\n"
        << "# Materials: " << scene.materials.size() << "\n"
        << "# Textures: " << scene.textures.size() << "\n"
        << "# Quad Lights: " << scene.quadLights.size() << "\n"
        << "# Point Lights: " << scene.pointLights.size() << "\n"
        << "# Cameras: " << scene.cameras.size() << "\n"
        << "# Texture Bytes: " << pretty_print_count(scene.total_texture_bytes()) << 'B';
    return ss.str();
}

SceneDescription::SceneDescription(const std::vector<std::string> &scene_files, Scene const& scene)
    : scene_files(scene_files)
    , info( BasicSceneState::get_scene_info(scene_files, scene) )
{
    for (const auto &scn: scene_files)
        ids.push_back(BasicSceneState::make_scene_id(scn));
    // todo: make more robust
    //center = scene.meshes[0].geometries[0].base + 0.5f * scene.meshes[0].geometries[0].extent;
    //radius = 0.5f * length(scene.meshes[0].geometries[0].extent);
}

void apply_selected_camera(Shell::DefaultArgs& config_args, Scene const& scene) {
    if (!config_args.got_camera_args && config_args.camera_id < scene.cameras.size()) {
        config_args.eye = scene.cameras[config_args.camera_id].position;
        config_args.center = scene.cameras[config_args.camera_id].center;
        config_args.up = scene.cameras[config_args.camera_id].up;
        config_args.fov_y = scene.cameras[config_args.camera_id].fov_y;
    }
}

void imstate_scene_loader_parameters(SceneLoaderParams& params, const std::vector<std::string> &fnames) {
    ImState::BeginRead();
    if (ImState::Open("SceneLoader")) {
        IMGUI_STATE1(ImGui::Checkbox, "use deduplication", &params.use_deduplication);
        IMGUI_STATE1(ImGui::Checkbox, "remove LODs", &params.remove_lods);
    }
    int scene_count = ilen(fnames);
    for (int scene_idx = 0; scene_idx < scene_count; ++scene_idx) {
        std::string scene_loader_id = "SceneLoader##" + get_file_name(fnames[scene_idx]);
        if (!ImState::Open(scene_loader_id.c_str()))
            continue;
        params.per_file.resize(scene_idx + 1);
        auto& per_file = params.per_file[scene_idx];

        IMGUI_STATE1(ImGui::DragInt, "remove first LODs", &per_file.remove_first_LODs);
        IMGUI_STATE1(ImGui::DragFloat, "instance pruning probability", &per_file.instance_pruning_probability);
        IMGUI_STATE1(ImGui::Checkbox, "small deformation", &per_file.small_deformation);
        IMGUI_STATE1(ImGui::Checkbox, "ignore textures", &per_file.ignore_textures);
        IMGUI_STATE1(ImGui::Checkbox, "ignore animation", &per_file.ignore_animation);
        IMGUI_STATE1(ImGui::Checkbox, "merge partition instances", &per_file.merge_partition_instances);
        IMGUI_STATE1(ImGui::Checkbox, "load specularity", &per_file.load_specularity);
    }
    ImState::EndRead();
}
