// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "imutils.h"
#include "imgui_internal.h"

bool ui_list_node_ex(char const* label, ImGuiTreeNodeFlags flags, bool *p_open, bool* p_up, bool* p_down) {
    flags |= ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_ClipLabelForTrailingButton | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    bool is_open = ImGui::TreeNodeEx(label, flags);
    if (p_open || p_up || p_down) {
        assert(p_open && p_up && p_down);
        auto& g = *ImGui::GetCurrentContext();

        // buttons analogously to https://github.com/ocornut/imgui/blob/master/imgui_widgets.cpp#L6141
        auto item_cursor = ImGui::GetCursorScreenPos();
        ImGuiLastItemData last_item_backup = g.LastItemData;
        ImGui::PushID(label);

        float inner_button_size = g.FontSize + 0.5f * g.Style.FramePadding.x;
        float total_button_size = g.Style.FramePadding.x + inner_button_size;
        float outer_frame_height = last_item_backup.Rect.Max.y - last_item_backup.Rect.Min.y;
        float outer_button_margin = 0.5f * (outer_frame_height - inner_button_size);
        float button_x = ImMax(last_item_backup.Rect.Min.x, last_item_backup.Rect.Max.x - total_button_size);
        float button_y = last_item_backup.Rect.Min.y;

        ImGui::SetCursorScreenPos(ImVec2(button_x - 2.0f * total_button_size + outer_button_margin, button_y + outer_button_margin));
        if (ImGui::ArrowButtonEx("#UP", ImGuiDir_Up, ImVec2(inner_button_size, inner_button_size)))
            *p_up = false;
        
        ImGui::SetCursorScreenPos(ImVec2(button_x - total_button_size + outer_button_margin, button_y + outer_button_margin));
        if (ImGui::ArrowButtonEx("#DOWN", ImGuiDir_Down, ImVec2(inner_button_size, inner_button_size)))
            *p_down = false;
        
        if (ImGui::CloseButton(ImGui::GetID("#CLOSE"), ImVec2(button_x, button_y)))
            *p_open = false;

        ImGui::PopID();
        ImGui::SetCursorScreenPos(item_cursor);
        g.LastItemData = last_item_backup;
    }
    if (is_open)
        ImGui::TreePush(label);
    return is_open;
}
