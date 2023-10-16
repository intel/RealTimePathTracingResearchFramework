// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "app_state.h"
#include "util/profiling.h"

#include <sstream>
#include <iomanip>
#include "types.h"

#include "../rendering/postprocess/reprojection.h"

bool BasicApplicationState::state(RenderBackend *renderer)
{
    bool other_changes = false;
    int user_target_spp = target_spp;
    if (IMGUI_STATE(ImGui::DragInt, "target spp", &user_target_spp, 1.0f, -1, INT_MAX / 2)) {
        if (user_target_spp <= 0)
            renderer->params.spp_accumulation_window = std::max(renderer->params.spp_accumulation_window, 64);
        else
            renderer->params.spp_accumulation_window = user_target_spp;
        other_changes = true;
        // only reconfigure target spp in interactive mode
        if (interactive()) {
            target_spp = user_target_spp;
            if (target_spp == 0)
                done_accumulating = true;
            else
#ifdef ENABLE_REALTIME_RESOLVE
            if (target_spp < 0 || renderer->params.reprojection_mode != REPROJECTION_MODE_NONE && !continuous_restart)
                done_accumulating = false;
            else
#endif
            {
                done_accumulating &= target_spp > 0 && accumulated_spp >= target_spp;
                renderer_changed |= target_spp > 0 && accumulated_spp > target_spp;
            }
        }
    }
    renderer_changed |= IMGUI_STATE(ImGui::SliderInt, "batch spp", &renderer->params.batch_spp, 1, 16);
    other_changes |= IMGUI_STATE(ImGui::Checkbox, "pause rendering", &pause_rendering);
    IMGUI_VOLATILE(ImGui::SameLine());
    other_changes |= IMGUI_STATE(ImGui::Checkbox, "continuous restart", &continuous_restart);
    renderer_changed |= IMGUI_STATE(ImGui::SliderInt, "max path depth", &renderer->params.max_path_depth, 1, MAX_PATH_DEPTH);

    bool russian_roulette_override;
    // for legacy configs
    if (IMGUI_OFFER(IMGUI_NO_UI, "enable russian roulette", &russian_roulette_override)) {
        if (russian_roulette_override && renderer->params.rr_path_depth >= MAX_PATH_DEPTH) {
            renderer->params.rr_path_depth = DEFAULT_RR_PATH_DEPTH;
            renderer_changed = true;
        } else if (!russian_roulette_override && renderer->params.rr_path_depth < MAX_PATH_DEPTH) {
            renderer->params.rr_path_depth = MAX_PATH_DEPTH;
            renderer_changed = true;
        }
    }
    renderer_changed |= IMGUI_STATE(ImGui::SliderInt, "rr path depth", &renderer->params.rr_path_depth, 1, MAX_PATH_DEPTH);

    bool glossy_mode = static_cast<bool>(renderer->params.glossy_only_mode);
    renderer_changed |= IMGUI_STATE(ImGui::Checkbox, "glossy-only mode", &glossy_mode);
    renderer->params.glossy_only_mode = static_cast<int>(glossy_mode);

    renderer_changed |= IMGUI_STATE(ImGui::Checkbox, "unroll bounces", &renderer->options.unroll_bounces);
#if defined(ENABLE_DYNAMIC_MESHES) // || defined(ENABLE_DYNAMIC_INSTANCS)
    renderer_changed |= IMGUI_STATE(ImGui::Checkbox, "force bvh rebuild", &renderer->options.force_bvh_rebuild);
    renderer_changed |= IMGUI_STATE(ImGui::SliderInt, "rebuild triangle budget", &renderer->options.rebuild_triangle_budget, 0, 10000000);
#endif

    // todo: move to extension?


    {
        static const char* const rng_variants[] = { RNG_VARIANT_NAMES };
        int &rng_variant = renderer->options.rng_variant;
        int last_active = std::max(std::min(rng_variant, array_ilen(rng_variants)-1), 0);
        if (IMGUI_STATE_BEGIN_ATOMIC_COMBO(ImGui::BeginCombo, "pointset", rng_variants, rng_variants[last_active])) {
            for (int i = 0; i < array_ilen(rng_variants); ++i) {
                if (IMGUI_STATE(ImGui::Selectable, rng_variants[i], i == last_active)) {
                    rng_variant = i;
                    renderer_changed = true;
                }
            }
            IMGUI_STATE_END(ImGui::EndCombo, rng_variants);
        }
    }
    // for legacy configs
    bool blue_noise_sampling = renderer->options.rng_variant == RNG_VARIANT_BN;
    if (IMGUI_OFFER(IMGUI_NO_UI, "blue noise sampling", &blue_noise_sampling))
        renderer->options.rng_variant = blue_noise_sampling ? RNG_VARIANT_BN : RNG_VARIANT_UNIFORM;

    renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "pixel radius", &renderer->params.pixel_radius, 0.05f, 4.0f);

    {
      static constexpr const char *output_channels[] = {OUTPUT_CHANNEL_NAMES};
      static constexpr int num_output_channels =
          sizeof(output_channels) / sizeof(output_channels[0]);

      int &oc = renderer->params.output_channel;
      const int last_active = std::max(std::min(oc, (int)num_output_channels - 1), 0);

      if (IMGUI_STATE_BEGIN_ATOMIC_COMBO(ImGui::BeginCombo,
                                         "output channel",
                                         output_channels,
                                         output_channels[last_active])) {
          for (int i = 0; i < (int)num_output_channels; ++i) {
              if (IMGUI_STATE(ImGui::Selectable, output_channels[i], i == last_active)) {
                  oc = i;
              }
          }
          IMGUI_STATE_END(ImGui::EndCombo, output_channels);
      }
    }

    renderer_changed |= IMGUI_STATE(ImGui::SliderInt, "output moment", &renderer->params.output_moment, 0, 1);
    renderer_changed |= IMGUI_STATE(ImGui::SliderFloat, "variance radius", &renderer->params.variance_radius, 0.001f, 32.f);

    if (!renderer_variants.empty()) {
        if (renderer_variants_support.size() != renderer_variants.size()) {
            renderer_variants_support.resize(renderer_variants.size(), (char) true);
            renderer->mark_unsupported_variants(renderer_variants_support.data());
        }
        int last_active = std::max(std::min(active_backend_variant, (int) renderer_variants.size() - 1), 0);
        char const* supported_flags = renderer_variants_support.data();
        bool find_new_supported = !supported_flags[last_active];
        auto pretty_variant_name = ImState::InDefaultMode() && last_active < (int) renderer_variants_pretty.size()
            ? renderer_variants_pretty[last_active].c_str()
            : renderer_variants[last_active].c_str();
        if (IMGUI_STATE_BEGIN_ATOMIC_COMBO(ImGui::BeginCombo, "variant", renderer_variants.data(), pretty_variant_name)) {
            for (int i = 0; i < (int) renderer_variants.size(); ++i){
                ImGuiSelectableFlags item_flags = !supported_flags[i] ? ImGuiSelectableFlags_Disabled : 0;
                auto pretty_variant_name = ImState::InDefaultMode() && i < (int) renderer_variants_pretty.size()
                    ? renderer_variants_pretty[i].c_str()
                    : renderer_variants[i].c_str();
                if ((IMGUI_STATE(ImGui::Selectable, pretty_variant_name, i == last_active, item_flags)
                    || find_new_supported) && supported_flags[i]) {
                    active_backend_variant = i;
                    renderer_changed |= true;
                    find_new_supported = false;
                }
            }
            IMGUI_STATE_END(ImGui::EndCombo, renderer_variants.data());
        }
    }

    renderer_changed |= IMGUI_STATE(ImGui::Checkbox, "force synchronous rendering", &synchronous_rendering);
    other_changes |= IMGUI_OFFER(ImGui::Checkbox, "freeze frame", &freeze_frame); // cmd-line-controllable, don't serialized automatically

    if (IMGUI_STATE_BEGIN_HEADER(ImGui::CollapsingHeader, "Filtering", &renderer->params.reprojection_mode, ImGuiTreeNodeFlags_DefaultOpen)) {
        static const char* const reprojection_operators[] = { REPROJECTION_MODE_NAMES };
        int &reprojection_mode = renderer->params.reprojection_mode;
        int last_active = std::max(std::min(reprojection_mode, array_ilen(reprojection_operators)-1), 0);
        if (IMGUI_STATE_BEGIN_ATOMIC_COMBO(ImGui::BeginCombo, "reprojection", reprojection_operators, reprojection_operators[last_active])) {
            for (int i = 0; i < array_ilen(reprojection_operators); ++i) {
                ImGuiSelectableFlags item_flags = 0;
#ifndef ENABLE_REALTIME_RESOLVE
                if (i > REPROJECTION_MODE_DISCARD_HISTORY)
                    item_flags = ImGuiSelectableFlags_Disabled;
#endif
                if (IMGUI_STATE(ImGui::Selectable, reprojection_operators[i], i == last_active, item_flags)) {
                    reprojection_mode = i;
                    renderer_changed = true;
                }
            }
            IMGUI_STATE_END(ImGui::EndCombo, reprojection_operators);
        }

#if defined(ENABLE_OIDN) || defined(ENABLE_OIDN2)
        renderer_changed |= IMGUI_STATE(ImGui::Checkbox, "enable denoising", &enable_denoising);
#endif

        bool upscale_2x = renderer->options.render_upscale_factor == 2;
        if (IMGUI_STATE(ImGui::Checkbox, "use 2x upscaling", &upscale_2x)) {
            renderer->options.render_upscale_factor = upscale_2x ? 2 : 1;
            renderer_changed = true;
        }

#ifdef ENABLE_REALTIME_RESOLVE
        const bool temporal_disabled = false;
#else
        IMGUI_VOLATILE(ImGui::BeginDisabled());
        const bool temporal_disabled = true;;
#endif

        renderer_changed |= IMGUI_STATE(ImGui::Checkbox, "TAA", &renderer->options.enable_taa);
        bool raster_taa_enabled = (renderer->params.enable_raster_taa != 0);
        bool force_unjittered_raster = (renderer->params.enable_raster_taa < 0);
        other_changes |= IMGUI_STATE(ImGui::Checkbox, "raster TAA pattern", &raster_taa_enabled);
        other_changes |= IMGUI_STATE(ImGui::Checkbox, "unjittered raster pattern", &force_unjittered_raster);
        if (force_unjittered_raster)
            renderer->params.enable_raster_taa = -1;
        else
            renderer->params.enable_raster_taa = static_cast<int>(raster_taa_enabled);

        if (temporal_disabled)
            IMGUI_VOLATILE(ImGui::EndDisabled());

        IMGUI_STATE_END_HEADER(&renderer->params.reprojection_mode);
    }

    return renderer_changed | other_changes;
}

int BasicApplicationState::add_variants(RenderBackend* renderer) {
    auto& variants = renderer->variant_names();
    if (variants.empty()) {
        renderer_variants.push_back(renderer->name());
        return 1;
    }
    auto& pretty_variants = renderer->variant_display_names();
    int i = 0;
    for (; i < (int) variants.size(); ++i) {
        renderer_variants.push_back(variants[i]);
        renderer_variants_pretty.push_back(i < (int) pretty_variants.size() ? pretty_variants[i] : variants[i]);
    }
    return i;
}

void BasicApplicationState::begin_after_initialization(
    Shell::DefaultArgs const& config_args,
    char const* change_tracking_file)
{
    this->change_tracking_file = change_tracking_file;
    this->framebuffer_format = config_args.image_format;

    if (config_args.freeze_frame)
        this->freeze_frame = true;

    if (config_args.validation_mode) {
        println(CLL::INFORMATION, "Validation mode active");
        target_spp = config_args.validation_target_spp;
        validation_img_prefix = config_args.validation_img_prefix;
        validation_mode = true;
    }

    else if (config_args.profiling_mode) {
        println(CLL::INFORMATION, "Profiling mode active");
        profiling_delta_time = 1.f / config_args.profiling_fps;
        profiling_img_prefix = config_args.profiling_img_prefix;
        profiling_mode = true;
    }

    else if (config_args.data_capture_mode) {
        println(CLL::INFORMATION, "Data capture mode active");
        data_capture_mode = true;
        data_capture_delta_time = 1.f / config_args.data_capture.fps;
        target_spp = config_args.data_capture.target_spp;
        data_capture = config_args.data_capture;
    }

    send_launch_signal(0);

    if (change_tracking_file) {
        change_tracking_timestamp = get_last_modified(change_tracking_file);
    }
}

bool BasicApplicationState::request_new_frame() {
    bool new_frame_loop = false;

    if (last_real_time < 0.0f) {
        last_real_time = shell.get_time();
        delta_real_time = 0.0f;
        delta_time = 0.0f;
        new_frame_loop = true;
    }

    bool new_frame = false;

    if (validation_mode) {
        // In validation mode, we render at a fixed time.
        // TODO: Support seeking in time and a validation time command line
        //       switch.
        current_time = 0;
        new_frame = (current_time != last_time);
    }
    else if (data_capture_mode)
    {
        new_frame = (current_time != last_time);
    }
    else
    {
        new_frame = (!new_frame_loop || current_time != last_time);
    }

    last_time = current_time;

    // while running a frame loop, always start a new frame
    return new_frame;
}

void BasicApplicationState::progress_time()
{
    double next_real_time = shell.get_time();
    delta_real_time = next_real_time - last_real_time;

    if (validation_mode) {
        // In validation mode, we render at a fixed time.
        delta_time = 0.0f;
    }
    else if (profiling_mode) {
        // In profiling mode, time progresses at a fixed, non-realtime
        // framerate.
        delta_time = profiling_delta_time;
        current_time += profiling_delta_time;
    }
    else if (data_capture_mode) {
        if (frame_ready) {
            delta_time = data_capture_delta_time;
            current_time += data_capture_delta_time;
            if (done_accumulating)
                reset_render();
        } else {
            delta_time = 0.f;
        }
    }
    else {
        delta_time = delta_real_time;
        current_time += delta_real_time;
    }

    last_real_time = next_real_time;
}

void BasicApplicationState::handle_shell_updates(Shell& shell)
{
    done |= shell.wants_quit;
    if (shell.was_reset) {
        renderer_changed = true;
        shell.was_reset = false;
    }
}

void BasicApplicationState::reset_render()
{
    done_accumulating = false;
    frame_ready = false;
    accumulated_spp = 0;
}


bool BasicApplicationState::save_framebuffer_png(const char *prefix,
    RenderBackend *renderer)
{
    const glm::uvec3 fbSize = renderer->get_framebuffer_size();
    const size_t bufferSize = fbSize.x * static_cast<size_t>(fbSize.y) * fbSize.z;
    readback_buffer_byte.resize(bufferSize);

    BasicProfilingScope readbackScope;
    readbackScope.begin();
    const bool available = bufferSize == renderer->readback_framebuffer(bufferSize, readback_buffer_byte.data());
    readbackScope.end();

    BasicProfilingScope saveScope;
    saveScope.begin();
    const bool written = WriteImage::write_png(prefix, fbSize.x, fbSize.y, fbSize.z,
        readback_buffer_byte.data());
    saveScope.end();

    return available && written;
}

bool BasicApplicationState::save_framebuffer_pfm(const char *prefix,
    RenderBackend *renderer)
{
    glm::uvec3 fbSize = renderer->get_framebuffer_size();
    const size_t bufferSize = fbSize.x * static_cast<size_t>(fbSize.y) * fbSize.z;
    readback_buffer_float.resize(bufferSize);

    BasicProfilingScope readbackScope;
    readbackScope.begin();
    const size_t nRead = renderer->readback_framebuffer(bufferSize, readback_buffer_float.data());
    readbackScope.end();

    bool available = nRead == bufferSize;
    if (nRead == bufferSize / renderer->options.render_upscale_factor / renderer->options.render_upscale_factor) {
        fbSize.x /= uint32_t(renderer->options.render_upscale_factor);
        fbSize.y /= uint32_t(renderer->options.render_upscale_factor);
        available = true;
    }

    BasicProfilingScope saveScope;
    saveScope.begin();
    const bool written = WriteImage::write_pfm(prefix, fbSize.x, fbSize.y, fbSize.z,
            readback_buffer_float.data());
    saveScope.end();

    return available && written;
}

bool BasicApplicationState::save_framebuffer_exr(const char *prefix,
    RenderBackend *renderer, ExrCompression compression)
{
    glm::uvec3 fbSize = renderer->get_framebuffer_size();
    const size_t bufferSize = fbSize.x * static_cast<size_t>(fbSize.y) * fbSize.z;
    readback_buffer_float.resize(bufferSize);

    BasicProfilingScope readbackScope;
    readbackScope.begin();
    const size_t nRead = renderer->readback_framebuffer(bufferSize, readback_buffer_float.data());
    readbackScope.end();

    bool available = nRead == bufferSize;
    if (nRead == bufferSize / renderer->options.render_upscale_factor / renderer->options.render_upscale_factor) {
        fbSize.x /= uint32_t(renderer->options.render_upscale_factor);
        fbSize.y /= uint32_t(renderer->options.render_upscale_factor);
        available = true;
    }

    BasicProfilingScope saveScope;
    saveScope.begin();
    const bool written = WriteImage::write_exr(prefix, fbSize.x, fbSize.y, fbSize.z,
            readback_buffer_float.data(), compression);
    saveScope.end();

    return available && written;
}

bool BasicApplicationState::save_framebuffer(const char *prefix,
    RenderBackend *renderer)
{
    return save_framebuffer(prefix, renderer, EXR_COMPRESSION_PIZ);
}

bool BasicApplicationState::save_framebuffer(const char *prefix, RenderBackend *renderer,
    ExrCompression compression)
{
    switch (framebuffer_format) {
        case OUTPUT_IMAGE_FORMAT_PNG:
          return save_framebuffer_png(prefix, renderer);
          break;
        case OUTPUT_IMAGE_FORMAT_PFM:
          return save_framebuffer_pfm(prefix, renderer);
          break;
        default:
          return save_framebuffer_exr(prefix, renderer, compression);
          break;
    }
    return false;
}

bool BasicApplicationState::save_aov_exr(const char *prefix,
        RenderBackend *renderer, RenderGraphic::AOVBufferIndex aovIndex,
        ExrCompression compression)
{
    const glm::uvec3 fbSize = renderer->get_framebuffer_size();
    const size_t bufferSize = fbSize.x * static_cast<size_t>(fbSize.y) * fbSize.z;
    readback_buffer_half.resize(bufferSize);
    
    BasicProfilingScope readbackScope;
    readbackScope.begin();
    const bool available = renderer->readback_aov(aovIndex, bufferSize,
        readback_buffer_half.data());
    readbackScope.end();

    BasicProfilingScope saveScope;
    saveScope.begin();
    const bool written = WriteImage::write_exr(prefix,
        fbSize.x, fbSize.y, fbSize.z, readback_buffer_half.data(),
        compression);
    saveScope.end();
    return available && written;
}

void BasicApplicationState::handle_mode_actions(const Shell &shell,
    RenderBackend* renderer)
{
    if (validation_mode)
    {
        if ((frame_ready || target_spp <= 0) 
         && !validation_img_prefix.empty())
        {
            std::ostringstream os;
            os << validation_img_prefix << "_"
               << std::setw(4) << std::setfill('0')
               << accumulated_spp;
            save_framebuffer(os.str().c_str(), renderer, EXR_COMPRESSION_PIZ);
        }

        if (done_accumulating)
            done = true;
    }
    else if (profiling_mode)
    {
        if (!profiling_img_prefix.empty()
         // In profiling mode, limit writing to once per second (at the end of the keyframe).
         && (current_time + profiling_delta_time) >= std::ceil(current_time))
        {
            std::ostringstream os;
            os << profiling_img_prefix << "_"
               << std::setw(4) << std::setfill('0')
               << ImState::CurrentKeyframe()+1;
            save_framebuffer(os.str().c_str(), renderer, EXR_COMPRESSION_NONE);
        }

        // In profiling mode, the last keyframe is a marker for exit.
        if (ImState::LastKeyframeComingUp(current_time + profiling_delta_time))
            done = true;
    }
    else if (data_capture_mode)
    {
        if (frame_ready)
        {
            std::ostringstream os;
            os << data_capture.img_prefix << "_"
               << std::setw(4) << std::setfill('0')
               << ImState::CurrentKeyframe()+1;
            const std::string pf = os.str();

            if (data_capture.rgba) {
                save_framebuffer((pf + "_rgba").c_str(), renderer, EXR_COMPRESSION_NONE);
            }
            if (data_capture.albedo_roughness) {
                save_aov_exr((pf + "_albedo_roughness").c_str(), renderer,
                    RenderGraphic::AOVAlbedoRoughnessIndex, EXR_COMPRESSION_NONE);
            }
            if (data_capture.normal_depth) {
                save_aov_exr((pf + "_normal_depth").c_str(), renderer,
                    RenderGraphic::AOVNormalDepthIndex, EXR_COMPRESSION_NONE);
            }
            if (data_capture.motion) {
                save_aov_exr((pf + "_motion_jitter").c_str(), renderer,
                    RenderGraphic::AOVMotionJitterIndex, EXR_COMPRESSION_NONE);
            }

            if (ImState::LastKeyframeComingUp(current_time + data_capture_delta_time))
            {
                done = true;
            }
        }
    }
    else
    {
        track_file_change(shell);
    }
}

void BasicApplicationState::track_file_change(const Shell &shell)
{
    if (!change_tracking_file || !interactive())
        return;

    const double time_since_start = shell.get_time();

    // Rate limiting.
    if (change_tracking_last_check + 0.5 >= time_since_start)
        return;

    unsigned long long timestamp = get_last_modified(change_tracking_file);
    change_tracking_last_check = time_since_start;
    if (timestamp > change_tracking_timestamp) {
        tracked_file_has_changed = true;
        change_tracking_timestamp = timestamp;
        done = true;
    }
}
