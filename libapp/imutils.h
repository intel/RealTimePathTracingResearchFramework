// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once
#include "imgui.h"

inline int valid_combo_index(int ia, char const* const* strings, size_t size = ~0) {
    int index = 0;
    for (int i = 0; i <= ia && (size_t) i < size && strings[i]; ++i)
        index = i;
    return index;
}

bool ui_list_node_ex(char const* label, ImGuiTreeNodeFlags flags, bool *p_open, bool* p_up, bool* p_down);

inline bool ui_begin_list_frame(char const* name, bool *p_add) {
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s: ", name);
    if (p_add) {
        ImGui::SameLine();
        *p_add = ImGui::Button("+");
    }
    return ImGui::BeginChildFrame( ImGui::GetID(name), ImVec2(0, 0) );
}

inline void ui_end_list_frame() {
    ImGui::EndChildFrame();
}
