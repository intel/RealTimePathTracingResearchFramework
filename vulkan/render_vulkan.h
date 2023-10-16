// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <vulkan/vulkan.h>
#include "render_backend.h"
#include "vulkan_utils.h"
#include "vulkanrt_utils.h"
#include "profiling/profiling_scopes.h"
#include <future>

#include "../librender/render_data.h"
#include "../librender/gpu_programs.h"
#include "../librender/lights.h"

namespace glsl {
    struct ViewParams;
    struct LocalParams;
    struct GlobalParams;
}

struct RenderPipelineVulkan;
struct RenderPipelineExtensionVulkan;
struct CustomPipelineExtensionVulkan;

namespace vkrt {
    struct RenderPipelineOptions;
    struct BindingLayoutCollector;
    struct BindingCollector;
}

struct LodGroup;

struct RenderVulkan : RenderBackend {
    vkrt::Device device;

    // todo: should this be read from device limits?
    static const int MAX_TEXTURE_COUNT = 1024 * 4;
    static const int MAX_LOD_MESH_COUNT = 1024 * 4;
    static const int MAX_DESC_SETS = 8;

    uint32_t base_arena_idx = 0;
    enum ArenaOffsets {
        StaticArenaOffset,
        DynamicArenaOffset,
        ArenaCount
    };

    enum BVHOperation {
        None,
        Refit,
        Rebuild
    };
    BVHOperation pending_tlas_request = BVHOperation::None;

    vkrt::Buffer local_param_buf = nullptr;
    vkrt::Buffer global_param_buf = nullptr;

    struct ParameterCache;
    std::unique_ptr<ParameterCache> cached_gpu_params;

    // Light data
    vkrt::Buffer light_data_buf = nullptr;
    std::vector<LightData> lightData;

    vkrt::Buffer instance_param_buf = nullptr;
    vkrt::Buffer instance_aabb_buf = nullptr;
    vkrt::Buffer parameterized_instance_buf = nullptr;
    vkrt::Buffer binned_light_params = nullptr; // export from resp. extension
    vkrt::Buffer img_readback_buf = nullptr;

    vkrt::Texture2D atomic_accum_buffers[2]; // provides storage for atomic_accum_buffer!
    vkrt::Texture2D accum_buffers[2];
    vkrt::Texture2D render_targets[2];
    vkrt::Texture2D depth_buffer = nullptr;
    int active_accum_buffer = 0;
    int active_render_target = 0;
    vkrt::Texture2D half_post_processing_buffers[2];
    vkrt::Texture2D current_color_buffer;
    VkSampler screen_sampler = VK_NULL_HANDLE;

    using RenderGraphic::AOVBufferIndex;
    vkrt::Texture2D aov_buffers[2 * AOVBufferCount];

#ifdef REPORT_RAY_STATS
    vkrt::Texture2D ray_stats = nullptr;
    vkrt::Buffer ray_stats_readback_buf = nullptr;
    std::vector<uint16_t> ray_counts;
#endif

    std::vector<std::unique_ptr<vkrt::TriangleMesh>> meshes;
    std::vector<std::vector<std::string>> mesh_shader_names; // note: indexed by mesh id
    std::vector<vkrt::ParameterizedMesh> parameterized_meshes;
    std::vector<std::vector<RenderMeshParams>> render_meshes; // note: indexed by parameterized mesh id!
    std::vector<std::vector<std::string>> shader_names; // note: indexed by parameterized mesh id
    std::vector<vkrt::Instance> instances;
    std::vector<LodGroup> lod_groups; // note: indexed by parameterized mesh id!
    std::vector<std::vector<uint32_t>> parameterized_instances; // note: indexed by parameterized mesh id!
    std::unique_ptr<vkrt::TopLevelBVH> scene_bvh;
    uint32_t scene_lod_group_count = 0; // need to store separately as lod_groups are indexed by param mesh id here... (but why?)
    unsigned meshes_revision = ~0;
    unsigned parameterized_meshes_revision = ~0;
    unsigned instances_revision = ~0;

    unsigned blas_generation = 0;
    unsigned blas_content_generation = 0;
    unsigned tlas_generation = 0;
    unsigned tlas_content_generation = 0;

    unsigned render_meshes_generation = 0;
    unsigned instance_params_generation = ~0;

    unsigned lights_revision = ~0;

    vkrt::Texture2D null_texture = nullptr;
    vkrt::Buffer null_buffer = nullptr;

    vkrt::Buffer mat_params = nullptr;
    std::vector<vkrt::Texture2D> textures;
    std::vector<vkrt::Texture2D> standard_textures;
    VkSampler sampler = VK_NULL_HANDLE;
    unsigned textures_revision = ~0;
    unsigned materials_revision = ~0;

    int swap_buffer_count = DEFAULT_SWAP_BUFFER_COUNT;
    int active_swap_buffer_count = swap_buffer_count;
    int swap_index = 0;
    VkEvent render_done_events[MAX_SWAP_BUFFERS] = { VK_NULL_HANDLE };
    VkFence render_done_fences[MAX_SWAP_BUFFERS] = { VK_NULL_HANDLE };

    VkDescriptorSetLayout null_desc_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textures_desc_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout standard_textures_desc_layout = VK_NULL_HANDLE;

    VkDescriptorPool texture_desc_pool = VK_NULL_HANDLE;
    VkDescriptorPool material_texture_desc_pool = VK_NULL_HANDLE;
    // We need a set per varying size array of things we're sending
    VkDescriptorSet textures_desc_set = VK_NULL_HANDLE;
    VkDescriptorSet standard_textures_desc_set = VK_NULL_HANDLE;

    std::vector<RenderPipelineExtensionVulkan*> available_pipeline_extensions;
    RenderBackendOptions active_options;
    enum struct options { use_configured_active_options_instead };
    uint16_t maxGeometrySBTParams = (uint16_t) sizeof(RenderMeshParams);

    struct PipelineStore {
        GpuProgramCache<RenderPipelineVulkan> pipelines;
        std::vector<char> support_flags;
        struct DeferredBuild {
            RenderPipelineVulkan* pipeline;
            std::future<void> build;
        };
        std::vector<DeferredBuild> prepared;
        unsigned hot_reload_generation = 0;
    } pipeline_store;

    vkrt::DescriptorSetUpdater desc_set_updater;

    std::unique_ptr<RenderPipelineVulkan> sample_processing_pipeline;

    vkrt::ProfilingData profiling_data;
    float rendering_time_ms = 0;

    size_t frame_id = 0;
    size_t frame_offset = 0;
    unsigned accumulated_spp = 0;
    bool accumulate_atomically = false;

    vkrt::Buffer ray_query_buffer = nullptr, ray_result_buffer = nullptr;
    int fixed_ray_query_budget = 0, per_pixel_ray_query_budget = 0;

    RenderVulkan(vkrt::Device const& device);
    virtual ~RenderVulkan();
    void internal_release_resources();

    ComputeDevice* create_compatible_compute_device() const override;
    std::string name() const override;
    std::vector<std::string> const& variant_names() const override;
    std::vector<std::string> const& variant_display_names() const override;
    void mark_unsupported_variants(char* support_flags) override;
    int variant_index(char const* name) override;

    std::vector<std::unique_ptr<RenderExtension>> create_default_extensions() override;
    std::unique_ptr<RenderExtension> create_processing_step(RenderProcessingStep step) override;

    void create_pipelines(RenderExtension** active_extensions, int num_extensions, RenderBackendOptions* forceOptions = nullptr) override;

    void initialize(const int fb_width, const int fb_height) override;
    void set_scene(const Scene &scene) override;
    void enable_ray_queries(const int max_queries, const int max_queries_per_pixel) override;
    void enable_aovs() override;

    void begin_frame(CommandStream* cmd_stream, const RenderConfiguration &config) override;
    void draw_frame(CommandStream* cmd_stream, int variant_idx) override;
    void end_frame(CommandStream* cmd_stream, int variant_idx) override;

    RenderStats render(const RenderConfiguration &config) override;
    RenderStats render(CommandStream* cmd_stream, const RenderConfiguration &config) override;
    bool render_ray_queries(int num_queries, const RenderParams &params, int variant_idx = 0, CommandStream* cmd_stream = nullptr) override;

    RenderStats stats() override;
    void flush_pipeline() override;
    void hot_reload() override;

    glm::uvec3 get_framebuffer_size() const override;
    size_t readback_framebuffer(size_t bufferSize, unsigned char *buffer, bool force_refresh) override;
    size_t readback_framebuffer(size_t bufferSize, float *buffer, bool force_refresh) override;
    size_t readback_aov(AOVBufferIndex aovIndex, size_t bufferSize, uint16_t *buffer, bool force_refesh) override;

    void update_config(SceneConfig const& config) override;
    void normalize_options(RenderBackendOptions& rbo, int variant_idx) const override;
    bool configure_for(RenderBackendOptions const& rbo, int variant_idx, AvailableRenderBackendOptions* available_recovery_options = nullptr) override;

    void register_descriptors(vkrt::BindingLayoutCollector collector, vkrt::RenderPipelineOptions const& options) const;
    int register_descriptor_sets(VkDescriptorSetLayout sets[MAX_DESC_SETS], uint32_t& push_constants_size, vkrt::RenderPipelineOptions const& options) const;
    int collect_descriptor_sets(VkDescriptorSet descriptor_sets[MAX_DESC_SETS], vkrt::RenderPipelineOptions const& options);

    glsl::GlobalParams* global_params(bool needs_update = false);
    glsl::LocalParams* local_params(bool needs_update = false);
    glsl::ViewParams *view_params(bool needs_update = false);
    glsl::ViewParams *ref_view_params(bool needs_update = false);
    RenderParams* render_params(bool needs_update = false);

    // Light functions
    std::vector<LightData> &light_data();
    void upload_light_data();

    vkrt::Texture2D& aov_buffer(AOVBufferIndex index) { return aov_buffers[index + AOVBufferCount * active_accum_buffer]; }
    vkrt::Texture2D& aov_history_buffer(AOVBufferIndex index) { return aov_buffers[index + AOVBufferCount * (1  - active_accum_buffer)]; }
    vkrt::Texture2D& accum_buffer() { return accum_buffers[active_accum_buffer]; }
    vkrt::Texture2D& render_target() { return render_targets[active_render_target]; }

    void update_shader_descriptor_table(vkrt::BindingCollector collector, vkrt::RenderPipelineOptions const& options, VkDescriptorSet desc_set);
    std::vector<RenderMeshParams> collect_render_mesh_params(int parameterized_mesh, Scene const& scene) const;
    void update_shader_binding_table(void* sbt_mapped, vkrt::ShaderBindingTable& table);

// internal:
    void prepare_raytracing_pipelines(bool defer_build);
    RenderPipelineVulkan* build_raytracing_pipeline(int variant_idx, const RenderBackendOptions& for_options
        , bool defer = false, bool* fallback_exists = nullptr);
    void lazy_update_shader_descriptor_table(RenderPipelineVulkan* pipeline, int swap_index
        , CustomPipelineExtensionVulkan* optional_managing_extension = nullptr);

    void update_view_parameters(const glm::vec3 &pos,
                                const glm::vec3 &dir,
                                const glm::vec3 &up,
                                const float fovy,
                                bool update_globals,
                                const glsl::ViewParams* vp_ref = nullptr);

    void async_refresh_global_parameters();

    void update_geometry(const Scene &scene, bool& update_sbt, bool& rebuild_tlas);
    void update_meshes(const Scene &scene, bool &update_sbt, bool &rebuild_sbt);
    void update_lights(const Scene &scene);
    void update_instances(const Scene &scene, bool rebuild_tlas);
    void default_update_tlas(std::unique_ptr<vkrt::TopLevelBVH>& scene_bvh, bool rebuild_tlas
        , int lod_offset, uint32_t instance_mask);
    void request_tlas_operation(BVHOperation op);
    bool has_pending_tlas_operations();
    void execute_pending_tlas_operations(VkCommandBuffer command_buffer);
    void update_tlas(bool rebuild_tlas);
    void update_instance_params();

    void update_textures(const Scene &scene);
    void update_materials(const Scene &scene);

    void update_sky_light(SceneConfig const& config);

    void record_frame(VkCommandBuffer command_buffer, int variant_idx, int num_rayqueries = 0, int samples_per_query = -1);
    void record_readback(VkCommandBuffer command_buffer, vkrt::Texture2D* target);

    template <class T>
    size_t readback_framebuffer_generic(size_t bufferSize, T *buffer,
        vkrt::Texture2D *texture);
};
