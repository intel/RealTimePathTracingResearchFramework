// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <vector>
#include "render_params.glsl.h"
#include "device_backend.h"
#include "../util/display/render_graphic.h"

extern bool running_rendering_profiling;

struct Scene;

struct RenderStats {
    float render_time = 0;
    float rays_per_second = 0;
    int   spp = 0;
    short frame_stats_delay = 0;
    bool has_valid_frame_stats = true;
    size_t total_device_bytes_allocated = 0;
    size_t max_device_bytes_allocated = 0;
    size_t device_bytes_currently_allocated = 0;
};

struct RenderCameraParams {
    glm::vec3 pos;
    glm::vec3 dir;
    glm::vec3 up;
    float fovy;
};

struct RenderConfiguration {
    RenderCameraParams camera;
    double time = 0.0;
    int active_variant = 0;
    int active_swap_buffer_count = -1;
    bool reset_accumulation = false;
    bool freeze_frame = false;
};

// mask of options that may be applied to a given current configuration
struct AvailableRenderBackendOptions {
#define RENDER_BACKEND_OPTIONS_DECLARE(type, name, default, flags) bool name GLCPP_DEFAULT(= false);
    RENDER_BACKEND_OPTIONS(RENDER_BACKEND_OPTIONS_DECLARE)
#undef RENDER_BACKEND_OPTIONS_DECLARE
};
struct GpuProgram;
RenderBackendOptions normalized_options(RenderBackendOptions options, AvailableRenderBackendOptions const* available_options, int for_stages
    , GpuProgram const* for_program = nullptr, AvailableRenderBackendOptions* query_available_options = nullptr);
bool equal_options(RenderBackendOptions const& a, RenderBackendOptions const& b, AvailableRenderBackendOptions const* available_options = nullptr);

struct RenderPipeline {
    //RenderBackendOptions options; // todo: fill? do we want this here or just a getter?

    RenderPipeline() = default;
    RenderPipeline(const RenderPipeline &) = delete;
    RenderPipeline& operator=(const RenderPipeline &) = delete;
    virtual ~RenderPipeline() { }
    virtual std::string name() = 0;
};
void get_defined_backend_options(RenderBackendOptions& options, char const* const* defines);
void get_defined_backend_options(RenderBackendOptions& options, struct GpuModuleDefine const* defines);

struct RenderExtension;
enum struct RenderProcessingStep : int;

struct RenderBackend : RenderGraphic {
    RenderBackendOptions options;
    RenderParams params;
    RenderCameraParams camera;
    LightSamplingConfig lighting_params;
    double time = 0.0;
    unsigned unique_scene_id = 0;
    bool reset_accumulation = false;
    bool freeze_frame = false;

    virtual ~RenderBackend() { }
    virtual std::string name() const = 0;
    virtual ComputeDevice* create_compatible_compute_device() const { return nullptr; }

    virtual void create_pipelines(RenderExtension** active_extensions, int num_extensions, RenderBackendOptions* forceOptions = nullptr) { }
    virtual std::vector<std::unique_ptr<RenderExtension>> create_default_extensions() { return {}; }
    virtual std::unique_ptr<RenderExtension> create_processing_step(RenderProcessingStep step);

    virtual void initialize(const int fb_width, const int fb_height) = 0;
    virtual std::vector<std::string> const& variant_names() const;
    virtual std::vector<std::string> const& variant_display_names() const { return variant_names(); }
    virtual void mark_unsupported_variants(char* support_flags) { } // changes respective entries of unsupported variants to 0
    virtual int variant_index(char const* name) { return 0; } // returns index for the requested variant, or -1 if not found, or 0 if unsupported (check support by result of variant_names)

    virtual void set_scene(const Scene &scene) = 0;
    virtual void update_config(SceneConfig const& config) { }
    virtual void normalize_options(RenderBackendOptions& rbo, int variant_idx) const { }
    virtual bool configure_for(RenderBackendOptions const& rbo, int variant_idx, AvailableRenderBackendOptions* available_recovery_options = nullptr) { return true; }

    virtual void begin_frame(CommandStream* cmd_stream, const RenderConfiguration &config);
    virtual void draw_frame(CommandStream* cmd_stream, int variant_idx = 0);
    virtual void end_frame(CommandStream* cmd_stream, int variant_idx = 0);

    virtual void enable_ray_queries(const int max_queries = DEFAULT_RAY_QUERY_BUDGET, const int max_queries_per_pixel = 0) { }
    virtual bool render_ray_queries(int num_queries, const RenderParams &params, int variant_idx = 0, CommandStream* cmd_stream = nullptr) { return false; }

    virtual void enable_aovs() { }

    // Returns the rays per-second achieved, or -1 if this is not tracked
    virtual RenderStats render(CommandStream* cmd_stream, const RenderConfiguration &config);
    virtual RenderStats stats();
    virtual void flush_pipeline() { }

    virtual void hot_reload() { }

protected: // deprecated
    std::unique_ptr<RenderStats> stats_cache;
    virtual RenderStats render(const RenderConfiguration &config) = 0;
};

struct Display;
typedef RenderBackend* (*create_backend_function)(Display& display);

// available backends
#ifdef ENABLE_VULKAN
RenderBackend* create_vulkan_backend(Display& display);
#endif

struct RenderExtension {
    unsigned last_initialized_generation = unsigned(~0);
    bool mute_flag = false;

    RenderExtension() = default;
    RenderExtension(RenderExtension const&) = delete;
    RenderExtension& operator=(RenderExtension const&) = delete;
    virtual ~RenderExtension() { }
    virtual std::string name() const = 0;
    
    virtual void initialize(const int fb_width, const int fb_height) = 0; // to be called after initialize on backend
    virtual void load_resources(const std::string& resource_dir) { } // allows the extension to load it's resources
    virtual bool ui_and_state(bool& renderer_changed) { return false; } // allows the extension to add UI & persistent state, return true if render restart required

    virtual char const* const* variant_names() { return nullptr; } // returns array of strings with terminating nullptr, if supported
    virtual int variant_index(char const* name) { return 0; } // returns index for the requested variant, or -1 if not found, or 0 if unsupported (check support by result of variant_names)

    virtual void update_scene_from_backend(const Scene& scene) = 0; // to be called after set_scene on backend

    virtual bool is_active_for(RenderBackendOptions const& rbo) const { return !mute_flag; }
    virtual void normalize_options(RenderBackendOptions& rbo) const { }
    virtual bool configure_for(RenderBackendOptions const& rbo, AvailableRenderBackendOptions* available_recovery_options = nullptr) { return true; }

    virtual void release_mapped_display_resources() { }
    virtual void release_mapped_scene_resources(const Scene* release_changes_only = nullptr) { }

    virtual void preprocess(CommandStream* cmd_stream, int variant_idx = 0) { }
    virtual void process(CommandStream* cmd_stream, int variant_idx = 0) { }
};

typedef std::unique_ptr<RenderExtension> (*create_backend_extension_function)(RenderBackend* backend);
template <class T> std::unique_ptr<RenderExtension> create_render_extension(RenderBackend* backend);

// available pre/post processing steps
enum struct RenderProcessingStep : int {
#define RENDER_PROCESSING_STEPS(declare) \
    declare(TAA) \
    declare(Example) \
    declare(UberPost) \
    declare(ProfilingTools) \
    declare(DepthOfField) \
    declare(OIDN2) \
    declare(DLDenoising) \
    declare(ReStir) \

// put steps into the enum
#define POST_PROCESSING_ENUM_DECLARE(name) name,
    RENDER_PROCESSING_STEPS(POST_PROCESSING_ENUM_DECLARE)
#undef POST_PROCESSING_ENUM_DECLARE
    Count // number of available steps
};
