// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_backend.h"
#include "gpu_programs.h"
#include "error_io.h"

bool running_rendering_profiling = false;

int RenderGraphic::DEFAULT_SWAP_BUFFER_COUNT = RenderGraphic::MAX_SWAP_BUFFERS;

std::vector<std::string> const& RenderBackend::variant_names() const {
    static const std::vector<std::string> none;
    return none;
}

void RenderBackend::begin_frame(CommandStream* cmd_stream, const RenderConfiguration &config) {
    this->params.render_upscale_factor = this->options.render_upscale_factor;
    this->camera = config.camera;
    this->time = config.time;
    this->reset_accumulation = config.reset_accumulation;
    this->freeze_frame = config.freeze_frame;
}
void RenderBackend::draw_frame(CommandStream* cmd_stream, int variant) {
    RenderConfiguration config = { camera };
    config.reset_accumulation = this->reset_accumulation;
    config.freeze_frame = this->freeze_frame;
    config.active_variant = variant;
    if (!stats_cache)
        stats_cache.reset(new RenderStats);
    *stats_cache = render(cmd_stream, config);
}
void RenderBackend::end_frame(CommandStream* cmd_stream, int variant_idx) {
}
RenderStats RenderBackend::render(CommandStream* cmd_stream, const RenderConfiguration &config) {
    return render(config);
}
RenderStats RenderBackend::stats() {
    return *stats_cache;
}

std::unique_ptr<RenderExtension> RenderBackend::create_processing_step(RenderProcessingStep step) {
    char const* step_name = "unknown";
    switch (step) {
#define RENDER_PROCESSING_STEP_CASE(step) \
    case RenderProcessingStep::step: \
        step_name = #step; \
        break; \

    RENDER_PROCESSING_STEPS(RENDER_PROCESSING_STEP_CASE)
#undef RENDER_PROCESSING_STEP_CASE
    default:
        break; // silence warning about count
    }
    throw_error("Unsupported post processing step \"%s\" (= %d)", step_name, (int) step);
    return nullptr;
}

RenderBackendOptions normalized_options(RenderBackendOptions options, AvailableRenderBackendOptions const* available_mask
    , int for_stages, GpuProgram const* for_program, AvailableRenderBackendOptions* query_available_options) {
    RenderBackendOptions normalized;
    if (for_program) {
        for_stages |= for_program->feature_flags;
        if (for_program->type == GPU_PROGRAM_TYPE_RASTERIZATION)
            for_stages |= RBO_STAGES_RASTERIZED;
        if (for_program->type == GPU_PROGRAM_TYPE_RAYTRACING)
            for_stages |= RBO_STAGES_RAYTRACED;
    }
    bool for_all_stages = (for_stages & RBO_STAGES_ALL) == RBO_STAGES_ALL || !(for_stages & RBO_STAGES_ALL);
    {
    #define backend_option_case(type, option, default, flags) \
        if (((flags) & for_stages) != 0 || for_all_stages && !((flags) & ~RBO_STAGES_CPU_ONLY)) { \
            if (!available_mask || available_mask->option) { \
                normalized.option = options.option; \
            } \
            if (query_available_options) { \
                query_available_options->option = true; \
            } \
        }
    RENDER_BACKEND_OPTIONS(backend_option_case)
    #undef backend_option_case
    }
    return normalized;
}

bool equal_options(RenderBackendOptions const& a, RenderBackendOptions const& b, AvailableRenderBackendOptions const* available_mask) {
    {
    #define backend_option_case(type, option, default, flags) \
        if ((!available_mask || available_mask->option) && a.option != b.option) { \
            return false; \
        }
    RENDER_BACKEND_OPTIONS(backend_option_case)
    #undef backend_option_case
    }
    return true;
}

void get_defined_backend_options(RenderBackendOptions& options, char const* const* defines) {
    for (int i = 0; defines[i]; ++i) {
        float fval = 0.0f;
        int ival = 0;

        #define backend_option_case(type, option, default, flags) \
            else if (typeid(type) == typeid(float) && \
                     1 == sscanf(defines[i], RENDER_BACKEND_OPTION_DEFINE(option) "=%f", &fval)) { \
                options.option = fval; \
            } else if (1 == sscanf(defines[i], RENDER_BACKEND_OPTION_DEFINE(option) "=%d", &ival)) { \
                options.option = (type) ival; \
            } else if(typeid(type) == typeid(bool) && \
                      strcmp(defines[i], RENDER_BACKEND_OPTION_DEFINE(option)) == 0) { \
                options.option = true; \
            }

        if (false) {
            // only here for else ifs ...
        } RENDER_BACKEND_OPTIONS(backend_option_case)
        #undef backend_option_case
    }
}

void get_defined_backend_options(RenderBackendOptions& options, GpuModuleDefine const* defines) {
    for (int i = 0; defines[i].name; ++i) {
        float fval = 0.0f;
        int ival = 0;

        // todo: use value once it is split from name!
        #define backend_option_case(type, option, default, flags) \
            else if (typeid(type) == typeid(float) && \
                     1 == sscanf(defines[i].name, RENDER_BACKEND_OPTION_DEFINE(option) "=%f", &fval)) { \
                options.option = fval; \
            } else if (1 == sscanf(defines[i].name, RENDER_BACKEND_OPTION_DEFINE(option) "=%d", &ival)) { \
                options.option = (type) ival; \
            } else if(typeid(type) == typeid(bool) && \
                      strcmp(defines[i].name, RENDER_BACKEND_OPTION_DEFINE(option)) == 0) { \
                options.option = true; \
            }

        if (false) {
            // only here for else ifs ...
        } RENDER_BACKEND_OPTIONS(backend_option_case)
        #undef backend_option_case
    }
}
