// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <memory>
#include <vector>
#include "imgui.h"
#include "imstate.h"
#include "util/display/display.h"
#include "librender/render_backend.h"
#include <GLFW/glfw3.h>
#include "util/display/imgui_backend.h"
#include "cmdline.h"
#include "shell.h"
#include "util.h"

extern bool running_rendering_profiling;

void Shell::readwrite_window_state() {
    IMGUI_STATE(ImGui::InputInt, "window x", &win_x);
    IMGUI_STATE(ImGui::InputInt, "window y", &win_y);
    IMGUI_STATE(ImGui::InputInt, "window width", &win_width);
    IMGUI_STATE(ImGui::InputInt, "window height", &win_height);
    IMGUI_STATE(ImGui::Checkbox, "window maximized", &win_maximized);
}

Shell shell;

int main(int argc, const char **argv)
{
    set_executable_path(argv[0]);
    detect_root_path("rendering/defaults.glsl");
    std::vector<std::string> vargs(argv, argv+argc);

    command_line::ProgramArgs args;
    try {
        // reset after executable path was resolved
        shell.cmdline_args = Shell::DefaultArgs();
        args = command_line::parse(&shell.cmdline_args, vargs);
    } catch (int error) {
        return error;
    }

    if (shell.cmdline_args.profiling_mode) {
        running_rendering_profiling = true;
        println(CLL::INFORMATION, "Running in profiling mode");
    }
    if (args.have_upscale_factor)
        shell.cmdline_args.fixed_upscale_factor = args.render_upscale_factor;
    shell.render_upscale_factor = args.render_upscale_factor;
    if (args.have_window_size) {
        shell.cmdline_args.fixed_resolution_x = args.window_width;
        shell.cmdline_args.fixed_resolution_y = args.window_height;
    }
    shell.win_width = args.window_width;
    shell.win_height = args.window_height;

    println(CLL::INFORMATION, "Frontend: %s", args.display_frontend.c_str());
    println(CLL::INFORMATION, "Backend: %s", shell.cmdline_args.renderer.c_str());
#ifdef COMPILING_FOR_DG2
    println(CLL::INFORMATION, "DG2 features are enabled");
#else
    println(CLL::INFORMATION, "DG2 features are disabled");
#endif

    if (!glfwInit()) {
        char const* error_msg = "unknown";
        glfwGetError(&error_msg);
        throw_error("Failed to init GLFW: %s", error_msg);
        return -1;
    }

    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    const std::vector<std::string> default_ini_search_path = {
        "", "configs/"
    };
    println(CLL::INFORMATION, "Default scene ini search paths:");
    for (const auto& sp: default_ini_search_path) {
        println(CLL::INFORMATION, "  <SCENE_FILE>/%s", sp.c_str());
    }
    println(CLL::INFORMATION, "Binary and resource paths: \"%s\", \"%s\"", get_executable_path(), shell.cmdline_args.resource_dir.c_str());

    ImState::RegisterApplicationSettings(*ImGui::GetCurrentContext());
    // skip default persistent state on validation
    if (shell.cmdline_args.validation_mode
     || shell.cmdline_args.profiling_mode
     || shell.cmdline_args.data_capture_mode) {
        ImState::SetApplicationIniFile(nullptr);
    }

    auto load_ini_settings = [&]() {
        // The scene ini file is loaded by default if it exists; However, it is
        // overwritten by all other ini files, including the application ini file.
        for (const auto &f: shell.cmdline_args.scene_files) {
            std::string default_scene_ini_path;

            const std::string default_scene_ini = get_file_basename(f) + ".ini";
            const std::string basepath = get_file_basepath(f);
            for (const auto& sp: default_ini_search_path) {
                const std::string p = basepath + "/" + sp + default_scene_ini;
                if (file_exists(p)) {
                    default_scene_ini_path = p;
                    break;
                }
            }
            if (default_scene_ini_path.empty()) {
                println(CLL::INFORMATION, "Cannot find default scene ini %s",
                    default_scene_ini.c_str());
            } else {
                println(CLL::INFORMATION, "Loading default scene ini %s",
                        default_scene_ini_path.c_str());
                ImState::LoadSettings(default_scene_ini_path.c_str());
            }
        }

        // load default settings if any
        ImState::LoadSettings();

        // add specific configuration if given
        for (auto& ini_file : args.configuration_inis) {
            if (file_exists(ini_file)) {
                println(CLL::INFORMATION, "Loading config from %s", ini_file.c_str());
                ImState::LoadSettings(ini_file.c_str());
            }
            else {
                throw_error("Cannot find config file: %s", ini_file.c_str());
            }
        }
        // add additional frames if given
        for (auto& frame : args.added_frames) {
            if (file_exists(frame.configuration_ini)) {
                println(CLL::INFORMATION, "Loading config from %s", frame.configuration_ini.c_str());
                int prevFrameCount = ImState::NumKeyframes();
                ImState::LoadSettings(frame.configuration_ini.c_str());
                // enforce given length for static configuration files
                if (ImState::NumKeyframes() == prevFrameCount)
                    ImState::AppendFrame(frame.hold);
            }
            else {
                throw_error("Cannot find config file: %s", frame.configuration_ini.c_str());
            }
        }

        // make sure there is at least one complete set of frames demarking start and end
        if (shell.cmdline_args.profiling_mode)
            ImState::PadFrames(1);
    };
    load_ini_settings();

    if (ImState::HaveNewSettings()) {
        ImState::BeginRead();
        if (ImState::Open())
            shell.readwrite_window_state();
        ImState::EndRead();
    }

    // apply any overrides to window size
    if (args.have_window_size || shell.win_width == 0 || shell.win_height == 0) {
        shell.win_width = args.window_width;
        shell.win_height = args.window_height;
    }
    // non-interactive modes always run in a fixed resolution,
    // even if provided by config files
    else if (shell.cmdline_args.validation_mode
          || shell.cmdline_args.profiling_mode
          || shell.cmdline_args.data_capture_mode) {
        shell.cmdline_args.fixed_resolution_x = shell.win_width;
        shell.cmdline_args.fixed_resolution_y = shell.win_height;
    }

    if (args.display_frontend == "gl") {
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

        glfwWindowHint(GLFW_DOUBLEBUFFER, GLFW_TRUE);
        glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
        glfwWindowHint(GLFW_DEPTH_BITS, 24);
        glfwWindowHint(GLFW_STENCIL_BITS, 8);
        
        glfwSwapInterval(0); // Disable vsync
    }
    else
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    if (shell.win_maximized)
        glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    shell.window = glfwCreateWindow(shell.win_width,
                                    shell.win_height,
                                    "Real-time Path Tracing Research Framework",
                                    nullptr, nullptr);
    if (!shell.window) {
        char const* error_msg = "unknown";
        glfwGetError(&error_msg);
        throw_error("Failed to create window: %s", error_msg);
        return -1;
    }
    shell.setup_event_handlers();

    if (shell.win_x != GLFW_WINDOWPOS_CENTERED && shell.win_y != GLFW_WINDOWPOS_CENTERED)
        glfwSetWindowPos(shell.window, shell.win_x, shell.win_y);

    std::exception_ptr eptr;
    bool eptr_logged = false;
#ifndef _DEBUG
    try
#endif
    {
        const char *device_override = nullptr;
        if (!args.device_override.empty()) {
            device_override = args.device_override.c_str();
        }
        std::unique_ptr<Display> display;
        if (args.display_frontend == "gl") {
            display.reset( create_opengl_display(shell.window, device_override) );
        }
#ifdef ENABLE_VULKAN
        else if (args.display_frontend == "vk") {
            display.reset( create_vulkan_display(shell.window, device_override) );
        }
#endif
        shell.display = display.get();
        shell.gui_init_events(); // finalize event handling after backend is created

        bool relaunch_app = false;
        do {
            if (relaunch_app) {
                ImState::ClearSettings(true);
                load_ini_settings();
            }

            relaunch_app = run_app(vargs);

            if (relaunch_app) {
                chrono_sleep(200);  // hacky :/ wait for executable to be fully written
                if (launch_sibling_process(vargs)) {
                    relaunch_app = false;  // hand over to child process
                    wait_for_signal(0);
                }
            }
        } while (relaunch_app);
#ifndef _DEBUG
    } catch (logged_exception const&) {
        eptr = std::current_exception();
        eptr_logged = true;
    } catch (std::exception const& e) {
        print_error("Exception caught: %s", e.what());
        eptr = std::current_exception();
    } catch (...) {
        eptr = std::current_exception();
#endif
    }

    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(shell.window);
    glfwTerminate();

    if (eptr) {
        if (!eptr_logged)
            std::rethrow_exception(eptr);
        return -1;
    }
    return 0;
}

std::unique_ptr<RenderBackend> Shell::create_standard_renderer(std::string const& name, Display* display) {
    std::unique_ptr<RenderBackend> renderer = nullptr;
    if (false) {
        // begin if-else-cascade
    }
#ifdef ENABLE_VULKAN
    else if (name == "vulkan") {
        renderer.reset( create_vulkan_backend(*display) );
    }
#endif
    assert(renderer);
    return renderer;
}
