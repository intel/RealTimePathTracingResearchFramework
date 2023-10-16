// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "backends/imgui_impl_glfw.cpp"

IMGUI_IMPL_API void ImGui_Backend_PadFrame(GLFWwindow* window, unsigned to_total_ms)
{/*
    ImGui_ImplSDL2_Data* bd = ImGui_ImplSDL2_GetBackendData();

    // Setup time step (we don't use SDL_GetTicks() because it is using millisecond resolution)
    static Uint64 frequency = SDL_GetPerformanceFrequency();
    Uint64 current_time = SDL_GetPerformanceCounter();
    if (bd->Time == 0 || bd->Time >= current_time) return;

    unsigned frame_time = unsigned((current_time - bd->Time) * 1000 / frequency);
    if (frame_time < to_total_ms)
        SDL_Delay(to_total_ms - frame_time);
*/}
