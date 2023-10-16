// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <array>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <memory>
#include <numeric>
#include <sstream>
#include <vector>

#include "interactive_camera.h"
#include "imstate.h"
#include "scene.h"
#include "util.h"
#include "profiling.h"
#include "shell.h"
#include "util/display/display.h"

#include "cmdline.h"
#include "app_state.h"
#include "scene_state.h"
#include "camera_state.h"
#ifdef ENABLE_DATACAPTURE
#include "data_capture_state.h"
#endif
#include "benchmark_info.h"

typedef BasicApplicationState ApplicationState;
typedef BasicSceneState SceneState;


extern char const* const USAGE;

enum Action {
    ACTION_TERMINATE_APP = 0,
    ACTION_SAVE_IMAGE,
    ACTION_NEXT_VARIANT,
    ACTION_TOGGLE_GUI,
    ACTION_HOT_RELOAD,
    ACTION_PLACE_CAPTURE_CAMERA,
    ACTION_NUM_ACTIONS
};

// Order as defined by the enum above.
static constexpr ImGuiKey keyMap[] = {
   ImGuiKey_Escape, // ACTION_TERMINATE_APP
   ImGuiKey_O, // ACTION_SAVE_IMAGE
   ImGuiKey_V, // ACTION_NEXT_VARIANT
   ImGuiKey_Period, // ACTION_TOGGLE_GUI
   ImGuiKey_F5, // ACTION_HOT_RELOAD
   ImGuiKey_Comma  // ACTION_PLACE_CAPTURE_CAMERA
};

static constexpr const char *actionName[] = {
   "Quit", // ACTION_TERMINATE_APP
   "Save Image", // ACTION_SAVE_IMAGE
   "Next Variant", // ACTION_NEXT_VARIANT
   "Toggle GUI", // ACTION_TOGGLE_GUI
   "Hot Reload", // ACTION_HOT_RELOAD
   "Place Capture Camera" // ACTION_PLACE_CAPTURE_CAMERA
};

bool run_app(std::vector<std::string> const& vargs)
{
    ImGuiIO &io = ImGui::GetIO();
    auto config_args = shell.cmdline_args;

    ProfilingScope profile_init("Initialization");

    std::unique_ptr<RenderBackend> renderer = Shell::create_standard_renderer(config_args.renderer, shell.display);
    renderer->options.render_upscale_factor = shell.render_upscale_factor;
    shell.delay_initialization = true;
    shell.initialize_display_and_renderer(renderer.get());
#if defined(ENABLE_CUDA)
    renderer->enable_ray_queries(DEFAULT_RAY_QUERY_BUDGET, 2);
#endif

    auto default_renderer_extensions = renderer->create_default_extensions();
    for (auto& ext : default_renderer_extensions)
        shell.initialize_renderer_extension(ext.get());

    ApplicationState app_state;
    int renderer_variant_count = app_state.add_variants(renderer.get());
    auto&& app_state_xi = [&]() {
        app_state.state(renderer.get());
    };

#ifdef ENABLE_DATACAPTURE
    DataCaptureTools data_capture_tools(renderer.get());
#endif

    renderer->create_pipelines(shell.renderer_extensions.data(), (int) shell.renderer_extensions.size());

#ifdef ENABLE_REALTIME_RESOLVE
    std::unique_ptr<RenderExtension> taa_postprocess = renderer->create_processing_step(RenderProcessingStep::TAA);
    shell.initialize_renderer_extension(taa_postprocess.get());
#endif

#ifdef ENABLE_EXAMPLES
    std::unique_ptr<RenderExtension> example_postprocess = renderer->create_processing_step(RenderProcessingStep::Example);
    shell.initialize_upscaled_processing_extension(example_postprocess.get());
#endif

#ifdef ENABLE_OIDN
    std::unique_ptr<RenderExtension> denoise_postprocess = renderer->create_processing_step(RenderProcessingStep::DLDenoising);
    shell.initialize_upscaled_processing_extension(denoise_postprocess.get());
#endif
#ifdef ENABLE_OIDN2
    std::unique_ptr<RenderExtension> oidn2_postprocess = renderer->create_processing_step(RenderProcessingStep::OIDN2);
    shell.initialize_renderer_extension(oidn2_postprocess.get());
#endif

#ifdef ENABLE_POST_PROCESSING
    // Create the uber post extension
    std::unique_ptr<RenderExtension> uberPostExtension = renderer->create_processing_step(RenderProcessingStep::UberPost);
    shell.initialize_upscaled_processing_extension(uberPostExtension.get());

    // Create the DOF extension
    std::unique_ptr<RenderExtension> depthOfFieldExtension = renderer->create_processing_step(RenderProcessingStep::DepthOfField);
    shell.initialize_renderer_extension(depthOfFieldExtension.get());
#endif

#ifdef ENABLE_PROFILING_TOOLS
    std::unique_ptr<RenderExtension> profiling_tools_extension = renderer->create_processing_step(RenderProcessingStep::ProfilingTools);
    shell.initialize_upscaled_processing_extension(profiling_tools_extension.get());
#endif


#ifdef ENABLE_DEBUG_VIEWS
    RenderExtension *debug_views_extension = nullptr;
    for (auto& ext : default_renderer_extensions)
    {
        if (ext->name() == "Vulkan Debug Views Extension") {
            debug_views_extension = ext.get();
        }
    }

#endif

    uint32_t numExtensions = shell.renderer_extensions.size();
    for (uint32_t extensionIdx = 0; extensionIdx < numExtensions; ++extensionIdx)
    {
        RenderExtension *extension = shell.renderer_extensions[extensionIdx];
        extension->load_resources(config_args.resource_dir);
    }

    SceneDescription scene_desc;
    {
        ProfilingScope profile_scene("Initialize Scene");

        SceneLoaderParams scene_loader_params;
        imstate_scene_loader_parameters(scene_loader_params, config_args.scene_files);
        if (config_args.deduplicate_scene)
            scene_loader_params.use_deduplication = true;

        ProfilingScope profile_read("Read Scene");
        Scene scene(config_args.scene_files, scene_loader_params);
        profile_read.end();

        scene_desc = SceneDescription(config_args.scene_files, scene);
        println(CLL::VERBOSE, "%s\n", scene_desc.info.c_str());

        {
            ProfilingScope profile_upload("Load Scene");
            shell.set_scene(scene);
#ifdef ENABLE_DATACAPTURE
            data_capture_tools.set_scene(scene);
#endif

            apply_selected_camera(config_args, scene);
        }
    }

    profile_init.end();
    log_profiling_times();

    OrientedCamera camera(config_args.up, config_args.eye, glm::quat_cast( glm::lookAt(config_args.eye, config_args.center, config_args.up) ));

    SceneState scene_state;
#ifdef ENABLE_DATACAPTURE
    DataCaptureState data_capture;
#endif

    bool camera_changed = false;
    auto&& scene_state_xi = [&]() {
        camera_changed |= camera_xi(camera);
        scene_state.state(renderer.get(), shell.renderer_extensions);
#ifdef ENABLE_DATACAPTURE
        data_capture.state(data_capture_tools, camera.eye());
#endif
    };

    auto settings_serialization = [&]() {
        if (ImState::Open())
            app_state_xi();
        for (const auto &id: scene_desc.ids) {
            if (ImState::Open(id.c_str()))
                scene_state_xi();
        }
    };
    std::string current_settings_source;
    for (ImState::SettingsHandler it; it.next(); ) {
        settings_serialization();
        // already keep track of first frame's source
        (void) ImState::NewSettingsSource(current_settings_source);
    }

    // command line overrides
    if (shell.cmdline_args.fixed_upscale_factor >= 1)
        renderer->options.render_upscale_factor = shell.cmdline_args.fixed_upscale_factor;

    app_state.begin_after_initialization(config_args, 
        // Detect changes to the application.
        get_executable_path());

    BenchmarkInfo benchmark_info;
    benchmark_info.rt_backend = renderer->name();
    benchmark_info.gpu_brand = shell.display->gpu_brand();
    benchmark_info.display_frontend = shell.display->name();
    if (app_state.profiling_mode) {
        for (auto extension : shell.renderer_extensions) {
            if (auto* csv_source = dynamic_cast<BenchmarkCSVSource*>(extension))
                benchmark_info.register_extended_benchmark_csv_source(csv_source);
        }
        benchmark_info.open_csv(config_args.profiling_csv_prefix + ".csv");
    }

    unsigned last_initialization_generation = 0;

    double motion_time = 0.0;
    bool show_ui = !config_args.disable_ui && app_state.interactive();
    uint64_t output_image_index = 0;
    std::string output_image_basename = "rptr_";
    {
        // Add hash based on program launch time to disambiguate image names.
        const auto ms_since_epoch = std::time(nullptr);;
        output_image_basename += std::to_string(ms_since_epoch);
    }
    auto last_working_renderer_options = renderer->options;
    while (!app_state.done) {
        bool new_frame = app_state.request_new_frame();
        bool new_shot = false;
        if (new_frame) {
            for (ImState::SettingsHandler it; it.next(app_state.current_time); ) {
                settings_serialization();
                new_shot = ImState::NewSettingsSource(current_settings_source);
            }
        }

        Shell::Event event;
        while (shell.poll_event(&event))
            shell.handle_event(event);
        app_state.handle_shell_updates(shell);
        shell.new_frame();

        camera_changed |= default_camera_movement(camera, shell, io, config_args);

        bool save_image = false;
        if (show_ui) {
            scene_state_xi();

            ImGui::Begin("Renderer");
            app_state_xi();
            if (ImGui::Button("Save Image"))
                save_image = true;
            ImGui::End();
        }

        if (!io.WantCaptureKeyboard) {
            if (ImGui::IsKeyPressed(keyMap[ACTION_TERMINATE_APP])) {
                app_state.done = true;
            } if (ImGui::IsKeyPressed(keyMap[ACTION_SAVE_IMAGE])) {
                save_image = true;
            }
        }
        if (!io.WantCaptureKeyboard) {
            // allow switching backend variance by key
            if (ImGui::IsKeyPressed(keyMap[ACTION_NEXT_VARIANT])) {
                if (!app_state.renderer_variants.empty()) {
                    app_state.active_backend_variant = std::max(app_state.active_backend_variant, 0) + (ImGui::IsKeyDown(ImGuiKey_ModShift) ?
                        ((int) app_state.renderer_variants.size()) - 1 :
                        1
                    );
                    app_state.active_backend_variant = app_state.active_backend_variant % (int) app_state.renderer_variants.size();
                    camera_changed = true;
                }
            }
            if (ImGui::IsKeyPressed(keyMap[ACTION_TOGGLE_GUI])) {
                show_ui = !show_ui;
            }
            if (ImGui::IsKeyPressed(keyMap[ACTION_HOT_RELOAD])) {
                renderer->hot_reload();
                app_state.renderer_changed = true;
            }
#ifdef ENABLE_DATACAPTURE
            // allow placing data capture cameras by key
            if (ImGui::IsKeyPressed(keyMap[ACTION_PLACE_CAPTURE_CAMERA])
                 && data_capture.pois.size())
            {
                //glm::vec3 newPos = rt_datacapture::random_walk(*raytracer, capture_rng, camera.eye(), 25, 40, 2.e32f, scene_center, scene_radius);
                rt_datacapture::View v = rt_datacapture::sample_viewpoint(*data_capture_tools.raytracer, data_capture.pois.data(), (int) data_capture.pois.size(), data_capture_tools.capture_rng);
                camera.set_position(v.pos);
                camera.set_direction(v.dir);
                camera_changed = true;
            }
#endif
        }

        bool reset_render =
            app_state.renderer_changed
         || new_shot
         || app_state.needs_rerender()
#ifdef ENE_VK_CUDA_NEURAL
         || learning_model_changed
#endif
            ;
#ifdef ENABLE_REALTIME_RESOLVE
        if (renderer->params.reprojection_mode == REPROJECTION_MODE_NONE)
#endif
        {
            reset_render |= camera_changed;
            reset_render |= scene_state.scene_changed;
        }
        if (reset_render)
            app_state.reset_render();
#ifndef ENABLE_REALTIME_RESOLVE
        if (app_state.renderer_changed || new_shot) {
#else
        if (app_state.renderer_changed) {
#endif
            renderer->flush_pipeline();
            benchmark_info.reset();
        }


        if ((app_state.accumulated_spp == 0 || renderer->params.reprojection_mode != REPROJECTION_MODE_NONE)
            && !app_state.freeze_frame) {
            if (app_state.interactive())
                motion_time += app_state.delta_time;
            else
                motion_time = app_state.current_time;
        }

        shell.display->new_frame();

        RenderStats stats;
        if (app_state.needs_render()) {
            int backup_batch_spp = renderer->params.batch_spp;
#ifdef ENABLE_REALTIME_RESOLVE
            if (renderer->params.reprojection_mode == 0)
#endif
                renderer->params.batch_spp = app_state.next_frame_spp(backup_batch_spp);

            RenderCameraParams camera_config = { camera.eye(), camera.dir(), camera.up(), config_args.fov_y };
            RenderConfiguration config = { camera_config };
            config.active_variant = app_state.active_backend_variant;
            config.reset_accumulation = app_state.accumulated_spp == 0;
            config.freeze_frame = app_state.freeze_frame;
            config.time = motion_time;

            bool synchronous_rendering = app_state.synchronous_rendering;

            if (!(app_state.active_backend_variant < renderer_variant_count)) {
                config.active_variant = -1;
                synchronous_rendering = true;
            }


#ifdef ENABLE_OIDN2
            oidn2_postprocess->mute_flag = !app_state.enable_denoising;
#endif
#ifdef ENABLE_OIDN
            denoise_postprocess->mute_flag = !app_state.enable_denoising;
#ifdef ENABLE_OIDN2
            oidn2_postprocess->mute_flag |= renderer->options.render_upscale_factor != 1;
            denoise_postprocess->mute_flag |= renderer->options.render_upscale_factor == 1;
#endif
#endif
            if (app_state.enable_denoising)
                synchronous_rendering = true;

            CommandStream* render_stream = shell.display->stream();
            if (synchronous_rendering) {
                render_stream = nullptr; // synchronous
                config.active_swap_buffer_count = 1;
            }

            // allow extensions to enforce consistent flags & features
            for (auto ext : shell.renderer_extensions)
                ext->normalize_options(renderer->options);
            // adapt features to the current main variant
            renderer->normalize_options(renderer->options, config.active_variant);

            for (int config_resolution_cycle = 0; ; ++config_resolution_cycle) {
                AvailableRenderBackendOptions rbo_mask;
                // attempt configuration with current main variant
                bool valid_config = renderer->configure_for(renderer->options, config.active_variant, &rbo_mask);
                for (auto ext : shell.renderer_extensions) {
                    if (!valid_config) break;
                    valid_config = ext->configure_for(renderer->options, &rbo_mask);
                }
                // accept valid configuration
                if (valid_config)
                    break;
                // recover from invalid configurations
                else {
                    // check if we previously had a different set of working options,
                    // i.e. program was not just started or reverted to previous config
                    if (equal_options(renderer->options, last_working_renderer_options))
                        throw_error("Broken configuration, please fix");
                    // 2nd recovery should have brought us back to equal options
                    assert(config_resolution_cycle < 2);

                    // strategy 1: automatic adaption to feature mask of current variant
                    if (config_resolution_cycle == 0) {
                        warning("Invalid combination of options detected, trying to adjust");
                        auto adjusted_rbo = normalized_options(renderer->options, &rbo_mask, RBO_STAGES_ALL);
                        if (!equal_options(adjusted_rbo, renderer->options))
                            renderer->options = adjusted_rbo;
                        else
                            ++config_resolution_cycle; // no adjustments made, fall through
                    }
                    // strategy 2: revert to previous working config
                    if (config_resolution_cycle == 1) {
                        warning("Could not adjust options to valid set, reverting to previous configuration");
                        renderer->options = last_working_renderer_options;
                    }
                }
            }

            bool needs_reinitialization = renderer->options.render_upscale_factor != shell.render_upscale_factor
                || shell.delay_initialization;
            for (auto ext : shell.renderer_extensions) {
                if (ext->is_active_for(renderer->options))
                    needs_reinitialization |= (ext->last_initialized_generation != last_initialization_generation);
            }

            // match any new requested render upscaling factor
            if (needs_reinitialization) {
                shell.render_upscale_factor = renderer->options.render_upscale_factor;
                shell.delay_initialization = false;
                shell.reinitialize_renderer_and_extensions();
                last_initialization_generation++;
                for (auto ext : shell.renderer_extensions) {
                    if (ext->is_active_for(renderer->options))
                        ext->last_initialized_generation = last_initialization_generation;
                }
            }

            renderer->begin_frame(render_stream, config);
            for (auto ext : shell.renderer_extensions)
                if (ext->is_active_for(renderer->options))
                    ext->preprocess(render_stream, config.active_variant);

            if (config.active_variant != -1)
                renderer->draw_frame(render_stream, config.active_variant);

            BasicProfilingScope extension_timer;


            extension_timer.end();


            renderer->end_frame(render_stream, config.active_variant >= 0 ? config.active_variant : 0);
            if (config.active_variant != -1)
                stats = renderer->stats();
            else {
                // todo: get this from wavefront PT / extensions directly
                stats.render_time = 0;
                stats.rays_per_second = -1;
            }

            if (stats.has_valid_frame_stats)
                stats.render_time += extension_timer.elapsedMS();

            bool moving_average = false;
#ifdef ENABLE_REALTIME_RESOLVE
            // reprojection modes have their own sliding window system, never done accumulating
            moving_average = renderer->params.reprojection_mode != 0;
#endif
            app_state.update_accumulated_spp(stats.spp, moving_average);
            renderer->params.batch_spp = backup_batch_spp;

#ifdef ENABLE_OIDN2
            if (!oidn2_postprocess->mute_flag)
                oidn2_postprocess->process(render_stream);
#endif
#ifdef ENABLE_OIDN
            if (!denoise_postprocess->mute_flag)
                denoise_postprocess->process(render_stream);
#endif

            // any post-accumulation post-processing
#ifdef ENABLE_POST_PROCESSING
            // any linear HDR processing
#ifdef ENABLE_EXAMPLES
            example_postprocess->process(render_stream);
#endif

#ifndef ENABLE_OIDN
            // todo: fix DoF + denoising
            depthOfFieldExtension->process(render_stream);
#endif

            // linear HDR to sRGB LDR transition
            uberPostExtension->process(render_stream);
#endif

#ifdef ENABLE_DEBUG_VIEWS
            debug_views_extension->process(render_stream);
#endif

            // any LDR post processing
#ifdef ENABLE_REALTIME_RESOLVE
            if (renderer->options.enable_taa
             && renderer->params.reprojection_mode != REPROJECTION_MODE_NONE)
                taa_postprocess->process(render_stream);
#endif
        }
        else
            stats.has_valid_frame_stats = false;

        scene_state.scene_changed = false;
        app_state.renderer_changed = false;
        camera_changed = false;

        last_working_renderer_options = renderer->options;

        if (show_ui) {
            ImGui::Begin("Render Info");
            benchmark_info.ui();
            ImGui::Text("%s", scene_desc.info.c_str());
            ImGui::Text("Accumulated Samples: %i", app_state.accumulated_spp);
            ImGui::Text("Accumulated Frames: %lu", benchmark_info.frames_accumulated);
            ImGui::Text("Memory currently allocated on device: %lu Mb",
                stats.device_bytes_currently_allocated / 1024 / 1024);
            ImGui::Text("Maximum memory allocated on device: %lu Mb",
                stats.max_device_bytes_allocated / 1024 / 1024);
            ImGui::Text("Total memory allocated on device: %lu Mb",
                stats.total_device_bytes_allocated / 1024 / 1024);

            ImGui::End();
            ImGui::Begin("Keyboard Shortcuts");
            for (int i = 0; i < ACTION_NUM_ACTIONS; ++i)
            {
                ImGui::Text("%s: %s", actionName[i],
                    ImGui::GetKeyName(keyMap[i]));
            }
            ImGui::End();
        }
        ImGui::Render();

        shell.display->display(renderer.get());

#ifdef ENABLE_PROFILING_TOOLS
        // Now that everything has ran, run the profiling.
        profiling_tools_extension->process(shell.display->stream());
#endif

        if (save_image) {
            std::ostringstream os;
            os << output_image_basename << "_"
               << std::setw(4) << std::setfill('0')
               << output_image_index++;
            app_state.save_framebuffer(os.str().c_str(), renderer.get());
        }

        app_state.handle_mode_actions(shell, renderer.get());

        // to limit frame rate: shell.pad_frame_time(1000 / 15);
        if (app_state.pause_rendering)
            shell.pad_frame_time(1000 / 11);
        else if (app_state.done_accumulating)
            shell.pad_frame_time(1000 / 31);

        app_state.progress_time();
        benchmark_info.aggregate_frame(stats.has_valid_frame_stats ? stats.render_time : 0.0f,
            1000.f * app_state.delta_real_time);

        if (app_state.profiling_mode)
            benchmark_info.write_csv();
    }

    if (app_state.interactive()) {
        for (ImState::SettingsWriter it; it.next(); ) {
            settings_serialization();
            if (ImState::Open())
                shell.readwrite_window_state(); // global app state
        }
    }

    return app_state.tracked_file_has_changed;
}
