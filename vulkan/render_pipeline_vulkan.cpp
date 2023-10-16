// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_vulkan.h"
#include "render_pipeline_vulkan.h"
#include "../librender/gpu_programs.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <string>

#include <glm/ext.hpp>
#include "types.h"
#include "scene.h"

#include "util.h"
#include "profiling.h"

RenderPipelineVulkan::RenderPipelineVulkan(RenderVulkan* backend
    , vkrt::RenderPipelineOptions const& pipeline_options)
    : pipeline_options(pipeline_options)
    , device(backend->device)
    , backend(backend)
{
    memset(&desc_frames, -1, sizeof(desc_frames));
}

RenderPipelineVulkan::~RenderPipelineVulkan() {
    internal_release_resources();
}

void RenderPipelineVulkan::internal_release_resources() {
    bool had_own_desc_set_and_pool = (desc_pool != VK_NULL_HANDLE);
    vkDestroyDescriptorPool(device->logical_device(), desc_pool, nullptr);
    desc_pool = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(device->logical_device(), pipeline_layout, nullptr);
    pipeline_layout = VK_NULL_HANDLE;
    if (had_own_desc_set_and_pool)
        vkDestroyDescriptorSetLayout(device->logical_device(), desc_layout, nullptr);
    desc_layout = VK_NULL_HANDLE;
}

void RenderPipelineVulkan::build_shader_descriptor_table(CustomPipelineExtensionVulkan* optional_managing_extension
    , VkDescriptorSetLayout inherited_desc_layout
    , VkFormat (*framebuffer_formats)[vkrt::BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS]
    , VkFormat *framebuffer_depth_format) {
    if (inherited_desc_layout) {
        desc_pool = VK_NULL_HANDLE;
        desc_layout = inherited_desc_layout;
        return;
    }

    vkrt::DescriptorSetLayoutBuilder builder;
    // allow early overlayed async updates while command buffer is still running
    builder.default_ext_flags |= VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

    VkFormat dummy_framebuffer_formats[vkrt::BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS];
    if (!framebuffer_formats)
        framebuffer_formats = &dummy_framebuffer_formats;
    VkFormat dummy_framebuffer_depth_format;
    if (!framebuffer_depth_format)
        framebuffer_depth_format = &dummy_framebuffer_depth_format;

    vkrt::BindingLayoutCollector blc = { builder, *framebuffer_formats, *framebuffer_depth_format };
    if (optional_managing_extension)
        optional_managing_extension->register_custom_descriptors(blc, this->pipeline_options);
    else
        backend->register_descriptors(blc, this->pipeline_options);
    // note: construction order matters to mark desc_layout as own
    desc_pool = builder.build_compatible_pool(*device, backend->swap_buffer_count);
    desc_layout = builder.build(*device);

    VkDescriptorSetLayout desc_set_layouts[RenderVulkan::MAX_SWAP_BUFFERS] = { };
    for (int i = 0; i < backend->swap_buffer_count; ++i)
        desc_set_layouts[i] = desc_layout;
    VkDescriptorSetAllocateInfo alloc_info = {};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = desc_pool;
    alloc_info.descriptorSetCount = backend->swap_buffer_count;
    alloc_info.pSetLayouts = desc_set_layouts;
    CHECK_VULKAN(vkAllocateDescriptorSets(device->logical_device(), &alloc_info, desc_sets));
}

void RenderPipelineVulkan::build_layout(VkShaderStageFlags push_constant_stages, CustomPipelineExtensionVulkan* optional_managing_extension) {
    VkPushConstantRange push_constants = {};
    push_constants.offset = 0;
    push_constants.size = this->pipeline_options.default_push_constant_size;
    push_constants.stageFlags = push_constant_stages;

    VkDescriptorSetLayout descriptor_layouts[backend->MAX_DESC_SETS] = { desc_layout };
    int desc_set_count = (optional_managing_extension)
        ? optional_managing_extension->register_custom_descriptor_sets(descriptor_layouts, push_constants.size, this->pipeline_options)
        : backend->register_descriptor_sets(descriptor_layouts, push_constants.size, this->pipeline_options);
    if (optional_managing_extension && desc_set_count == 0)
        desc_set_count = 1; // default set collection
    assert(desc_set_count <= backend->MAX_DESC_SETS);

    VkPipelineLayoutCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create_info.setLayoutCount = desc_set_count;
    pipeline_create_info.pSetLayouts = descriptor_layouts;
    if (push_constants.stageFlags && push_constants.size) {
        pipeline_create_info.pPushConstantRanges = &push_constants;
        pipeline_create_info.pushConstantRangeCount = 1;
    }

    CHECK_VULKAN(vkCreatePipelineLayout(
        device->logical_device(), &pipeline_create_info, nullptr, &pipeline_layout));
    this->push_constant_stages = push_constant_stages;
}

void RenderPipelineVulkan::update_shader_descriptor_table(vkrt::DescriptorSetUpdater& updater, int swap_index
    , CustomPipelineExtensionVulkan* optional_managing_extension) {
    vkrt::Texture2D dummy_framebuffer_formats[vkrt::BindingCollector::MAX_FRAMEBUFFER_BINDINGS];
    vkrt::Texture2D dummy_framebuffer_depth_format;
    vkrt::BindingCollector blc = { updater, dummy_framebuffer_formats, dummy_framebuffer_depth_format };

    if (optional_managing_extension)
        optional_managing_extension->update_custom_shader_descriptor_table(blc, this->pipeline_options, desc_sets[swap_index]);
    else
        backend->update_shader_descriptor_table(blc, this->pipeline_options, desc_sets[swap_index]);
}

void RenderPipelineVulkan::bind_pipeline(VkCommandBuffer render_cmd_buf
    , void const* push_constants, size_t push_size
    , int swap_index, CustomPipelineExtensionVulkan* optional_managing_extension) {
    vkCmdBindPipeline(
        render_cmd_buf, pipeline_bindpoint, pipeline_handle);

    if (push_constants && push_constant_stages) {
        vkCmdPushConstants(render_cmd_buf, pipeline_layout, push_constant_stages,
            0, push_size, push_constants);
    }

    // note: last element should always stay sentinel null!
    VkDescriptorSet descriptor_sets[backend->MAX_DESC_SETS + 1] = { desc_sets[swap_index] };
    int desc_set_count = optional_managing_extension
        ? optional_managing_extension->collect_custom_descriptor_sets(descriptor_sets, this->pipeline_options)
        : backend->collect_descriptor_sets(descriptor_sets, this->pipeline_options);
    if (optional_managing_extension && desc_set_count == 0)
        desc_set_count = 1; // default set collection
    assert(desc_set_count <= backend->MAX_DESC_SETS);
    assert(descriptor_sets[backend->MAX_DESC_SETS] == VK_NULL_HANDLE);

    int desc_set_begin = 0;
    // note: iterates one past last element to flush on sentinel null
    for (int i = 0; i <= desc_set_count; ++i) {
        if (descriptor_sets[i] != VK_NULL_HANDLE)
            continue;
        // end of consecutive non-null range
        if (i != desc_set_begin) {
            vkCmdBindDescriptorSets(render_cmd_buf,
                                    pipeline_bindpoint,
                                    pipeline_layout,
                                    desc_set_begin, (uint32_t) i - desc_set_begin, &descriptor_sets[desc_set_begin],
                                    0, nullptr);
        }
        // earliest begin of next non-null range
        desc_set_begin = i + 1;
    }
    // all non-null ranges handled
    assert(desc_set_begin == desc_set_count+1);
}

ComputeRenderPipelineVulkan::ComputeRenderPipelineVulkan(RenderVulkan* backend, GpuProgram const* program
    , vkrt::RenderPipelineOptions const& pipeline_options
    , bool defer
    , CustomPipelineExtensionVulkan* optional_managing_extension
    , char const* compiler_options
    , VkDescriptorSetLayout inherited_desc_layout)
    : RenderPipelineVulkan(backend, pipeline_options) {
    this->source_program = program;
    if (compiler_options)
        this->source_compile_options = compiler_options;
    this->source_managing_extension = optional_managing_extension;
    this->pipeline_bindpoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    try {
        build_shader_descriptor_table(optional_managing_extension, inherited_desc_layout);
        build_layout(VK_SHADER_STAGE_COMPUTE_BIT, optional_managing_extension);
        build_pipeline(program, compiler_options, defer);
    }
    catch (...) {
        internal_release_resources();
        throw;
    }
}

ComputeRenderPipelineVulkan::~ComputeRenderPipelineVulkan() {
    internal_release_resources();
}

void ComputeRenderPipelineVulkan::internal_release_resources() {
    vkDestroyPipeline(device->logical_device(), pipeline_handle, nullptr);
    pipeline_handle = VK_NULL_HANDLE;
}

bool ComputeRenderPipelineVulkan::hot_reload(std::unique_ptr<RenderPipelineVulkan>& next_pipeline, unsigned for_generation) {
    if (for_generation == this->hot_reload_generation)
        return false;

    bool needs_rebuild = source_program && gpu_program_binary_changed(source_program, pipeline_options, source_compile_options.c_str());
    this->hot_reload_generation = for_generation;

    if (needs_rebuild) {
        bool had_own_desc_set_and_pool = (desc_pool != VK_NULL_HANDLE);
        std::unique_ptr<RenderPipelineVulkan> new_pipeline{
            new ComputeRenderPipelineVulkan(backend, source_program, pipeline_options, false
                , source_managing_extension
                , source_compile_options.c_str()
                , had_own_desc_set_and_pool ? VK_NULL_HANDLE : desc_layout
                )
        };
        new_pipeline->hot_reload_generation = for_generation;
        next_pipeline = std::move(new_pipeline);
        return true;
    }
    return false;
}

std::string ComputeRenderPipelineVulkan::name() {
    // todo: store the source names?
    return "Compute Render Pipeline";
}

void ComputeRenderPipelineVulkan::wait_for_construction() {
    if (deferred_module) {
        CHECK_VULKAN( build_compute_pipeline(*device, &pipeline_handle, pipeline_layout, deferred_module) );
        deferred_module = nullptr;
    }
}

bool ComputeRenderPipelineVulkan::build_pipeline(GpuProgram const* program, char const* compiler_options, bool defer) {
    make_gpu_program_binaries(program, pipeline_options, compiler_options);

    assert(program->modules[0] && !program->modules[1]);
    assert(program->modules[0]->units[0] && !program->modules[0]->units[1]);
    auto compute_unit = program->modules[0]->units[0];

    auto compute_shader = vkrt::ShaderModule(*device
        , read_gpu_shader_binary(compute_unit, pipeline_options, compiler_options));
    std::vector<std::string> string_store;
    vkrt::get_workgroup_size(merge_to_old_defines(compute_unit->defines, string_store).data(), &workgroup_size.x, &workgroup_size.y, &workgroup_size.z);

    if (defer) {
        deferred_module = compute_shader;
        return false;
    }
    CHECK_VULKAN( build_compute_pipeline(*device, &pipeline_handle, pipeline_layout, compute_shader) );
    return true;
}

void ComputeRenderPipelineVulkan::dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) {
    glm::uvec3 workgroup_dim = (glm::uvec3) workgroup_size;
    glm::uvec3 dispatch_dim = (glm::uvec3(width, height, batch_spp) + workgroup_dim - glm::uvec3(1)) / workgroup_dim;

    vkCmdDispatch(render_cmd_buf,
        dispatch_dim.x,
        dispatch_dim.y,
        dispatch_dim.z);
}


RayTracingPipelineVulkan::RayTracingPipelineVulkan(RenderVulkan* backend, GpuProgram const* program
    , VkShaderStageFlags push_constant_stages, vkrt::RenderPipelineOptions const& pipeline_options
    , bool defer
    , CustomPipelineExtensionVulkan* optional_managing_extension)
    : RenderPipelineVulkan(backend, pipeline_options) {
    this->source_program = program;
    this->pipeline_bindpoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    this->pipeline_options.enable_raytracing = true;
    push_constant_stages |= VK_SHADER_STAGE_RAYGEN_BIT_KHR
        | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
        | VK_SHADER_STAGE_MISS_BIT_KHR
        | VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    try {
        build_shader_descriptor_table(optional_managing_extension);
        build_layout(push_constant_stages, optional_managing_extension);
        build_pipeline(program, defer);
    }
    catch (...) {
        internal_release_resources();
        throw;
    }
}

RayTracingPipelineVulkan::~RayTracingPipelineVulkan() {
    internal_release_resources();
}

void RayTracingPipelineVulkan::internal_release_resources() {
    rt_pipeline = nullptr;
    shader_table = { };
}

std::string RayTracingPipelineVulkan::name() {
    // todo: store the source names?
    return "Ray Tracing Pipeline";
}

void RayTracingPipelineVulkan::wait_for_construction() {
    if (rt_pipeline.handle() != VK_NULL_HANDLE) {
        rt_pipeline.wait_for_construction();
        this->pipeline_handle = rt_pipeline.handle();
    }
}

bool RayTracingPipelineVulkan::build_pipeline(GpuProgram const* program, bool defer) {
    make_gpu_program_binaries(program, pipeline_options);

    auto raygen_unit = gpu_module_single_unit(program, "raygen");
    auto miss_unit = gpu_module_single_unit(program, "miss");
    auto occlusion_miss_unit = gpu_module_single_unit(program, "occlusion_miss");

// Todo: Some issue in the validation layers prevents us from doing the work asynchronously (version 1.3.211)
// Make sure to remove this when a new version comes out
#if defined(_DEBUG)
    defer = false;
#endif

    auto raygen_shader = vkrt::ShaderModule(*device
        , read_gpu_shader_binary(raygen_unit, pipeline_options) );

    int recursion_depth = 1;
    for (int i = 0; raygen_unit->defines[i].name; ++i) {
        static char const RECURSION_DEPTH_DEFINE[] = "RECURSION_DEPTH=";
        // todo: change with split defines
        if (char const* recursion_depth_define = strstr(raygen_unit->defines[i].name, RECURSION_DEPTH_DEFINE)) {
            recursion_depth_define += sizeof(RECURSION_DEPTH_DEFINE) - 1;
            int rec_depth = 0;
            if (sscanf(recursion_depth_define, "%d", &rec_depth) && rec_depth)
                recursion_depth = rec_depth > 0 ? rec_depth : MAX_PATH_DEPTH;
            else if (strcmp(recursion_depth_define, "MAX_PATH_DEPTH") == 0)
                recursion_depth = MAX_PATH_DEPTH;
        }
    }

    auto miss_shader = vkrt::ShaderModule(*device
        , read_gpu_shader_binary(miss_unit, pipeline_options));
    auto occlusion_miss_shader = vkrt::ShaderModule(*device
        , read_gpu_shader_binary(occlusion_miss_unit, pipeline_options));

    auto rtPipelineBuilder = vkrt::RTPipelineBuilder()
                    .set_raygen("raygen", raygen_shader)
                    .add_miss("miss", miss_shader)
                    .add_miss("occlusion_miss", occlusion_miss_shader)
                    .set_recursion_depth(recursion_depth)
                    .set_layout(pipeline_layout);


    bool have_default_hit = false;
    for (int module_idx = 0; program->modules[module_idx]; ++module_idx) {
        auto module = program->modules[module_idx];
        if (strcmp(module->type, "rchit") != 0)
            continue;
        auto closest_hit_unit = gpu_module_single_unit_typed(module, "rchit");
        auto any_hit_unit = gpu_module_single_unit_typed(module, "rahit", true);

        std::string groupname = module->name;
        have_default_hit |= groupname == "hit";

        auto closest_hit_shader = vkrt::ShaderModule(*device
            , read_gpu_shader_binary(closest_hit_unit, pipeline_options));

        vkrt::ShaderModule any_hit_shader = nullptr;
        if (any_hit_unit) {
            any_hit_shader = vkrt::ShaderModule(*device
                , read_gpu_shader_binary(any_hit_unit, pipeline_options));
        }

        rtPipelineBuilder.add_hitgroup(groupname, closest_hit_shader);

        if (any_hit_shader) {
            std::string alpha_groupname = groupname + "_alpha";
            rtPipelineBuilder.add_hitgroup(alpha_groupname, closest_hit_shader);
            rtPipelineBuilder.add_hitgroup(alpha_groupname, any_hit_shader,
                VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
        }
    }
    if (!have_default_hit)
        warning("RT Pipeline %s does not contain a default hit group named 'hitgroup' (no closest hit shader named 'hit')", name().c_str());

    rt_pipeline = rtPipelineBuilder.build(*device, defer);

    if (!defer)
        this->pipeline_handle = rt_pipeline.handle();
    return !defer;
}

void RayTracingPipelineVulkan::build_shader_binding_table()
{
    vkrt::SBTBuilder sbt_builder;
    sbt_builder.set_raygen(vkrt::ShaderRecord{"raygen", rt_pipeline.shader_ident("raygen", true), sizeof(uint32_t)})
        .add_miss(vkrt::ShaderRecord{"miss", rt_pipeline.shader_ident("miss", true), 0})
        .add_miss(vkrt::ShaderRecord{"occlusion_miss", rt_pipeline.shader_ident("occlusion_miss", true), 0});

    int hit_group_count = 0;
    int any_hit_group_count = 0;

    // todo: make sure this is always available und fully constructed etc.
    auto const& render_meshes = backend->render_meshes;


    bool emitted_missing_hitprogram_warning = false;
    bool supports_extended_default_shaders = (source_program && source_program->feature_flags & GPU_PROGRAM_FEATURE_EXTENDED_HIT);

    for (size_t i = 0; i < backend->parameterized_meshes.size(); ++i) {
        std::vector<RenderMeshParams> const &hit_group_params = render_meshes[i];
        const auto &shader_names = backend->shader_names[i];
        for (size_t j = 0; j < hit_group_params.size(); ++j) {
            bool no_alpha = (hit_group_params[j].flags & GEOMETRY_FLAGS_NOALPHA) != 0;
            bool extended_shader = (hit_group_params[j].flags & GEOMETRY_FLAGS_EXTENDED_SHADER) != 0;
            bool thin_shader = (hit_group_params[j].flags & GEOMETRY_FLAGS_THIN) != 0;
            const std::string mesh_hg_name =
                "HitGroup_param_mesh" + std::to_string(i) + "_geom" + std::to_string(j);
            std::string hg_name;
            if (j < shader_names.size())
                hg_name = shader_names[j];
            if (hg_name.empty() || hg_name[0] == '+') {
                // default shader complexity assignment, e.g.
                // transmission and thin transmission are rarely used,
                // therefore enabled on demand
                if (supports_extended_default_shaders && extended_shader) {
                    if (thin_shader)
                        hg_name.insert(0, "hit_extended_thin");
                    else
                        hg_name.insert(0, "hit_extended");
                }
                else
                    hg_name.insert(0, "hit");
            }

            uint8_t const* shader_ident = nullptr;
            if (!no_alpha)
                shader_ident = rt_pipeline.shader_ident(hg_name + "_alpha");
            if (!shader_ident)
                shader_ident = rt_pipeline.shader_ident(hg_name);
            else
                ++any_hit_group_count;
            if (!shader_ident) {
                if (!emitted_missing_hitprogram_warning) {
                    warning("Shader identifier \"%s\" not found, using default!", hg_name.c_str());
                    emitted_missing_hitprogram_warning = true;
                }
                std::string hg_name = "hit";
                if (!no_alpha)
                    shader_ident = rt_pipeline.shader_ident(hg_name + "_alpha");
                if (!shader_ident)
                    shader_ident = rt_pipeline.shader_ident(hg_name);
                else
                    ++any_hit_group_count;
            }
            if (!shader_ident)
                throw_error("Shader identifier \"%s\" not found!", hg_name.c_str());
            sbt_builder.add_hitgroup(vkrt::ShaderRecord{mesh_hg_name, shader_ident, backend->maxGeometrySBTParams});

            ++hit_group_count;
        }
    }

    println(CLL::VERBOSE, "%d any hit-shader groups of %d hit groups", any_hit_group_count, hit_group_count);

    shader_table = sbt_builder.build(
          vkrt::MemorySource(*device, backend->base_arena_idx + backend->StaticArenaOffset));

    unique_scene_id = backend->unique_scene_id;
    parameterized_meshes_revision = backend->parameterized_meshes_revision;
}

void RayTracingPipelineVulkan::update_shader_binding_table() {
    if (unique_scene_id != backend->unique_scene_id
     || parameterized_meshes_revision != backend->parameterized_meshes_revision) {
        this->build_shader_binding_table();
        --render_meshes_generation; // invalidate
    }
    if (render_meshes_generation == backend->render_meshes_generation)
        return;

    auto sbt_upload_buffer = shader_table.upload_buffer();
    void* sbt_mapped = sbt_upload_buffer.map();

    backend->update_shader_binding_table(sbt_mapped, shader_table);

    sbt_upload_buffer.unmap();

    auto sync_commands = device.sync_command_stream();
    {
        sync_commands->begin_record();

        BUFFER_BARRIER(sbt_barrier);
        sbt_barrier.buffer = shader_table.buffer();
        sbt_barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        {
            vkrt::MemoryBarriers<1, 1> mem_barriers;
            mem_barriers.add(VK_PIPELINE_STAGE_TRANSFER_BIT, sbt_barrier);
            mem_barriers.set(sync_commands->current_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        }

        VkBufferCopy copy_cmd = {};
        copy_cmd.size = sbt_upload_buffer->size();
        vkCmdCopyBuffer(sync_commands->current_buffer,
                        sbt_upload_buffer->handle(),
                        shader_table.buffer()->handle(),
                        1,
                        &copy_cmd);

        sbt_barrier.srcAccessMask = sbt_barrier.dstAccessMask;
        sbt_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        {
            vkrt::MemoryBarriers<1, 1> mem_barriers;
            mem_barriers.add(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, sbt_barrier);
            mem_barriers.set(sync_commands->current_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        sync_commands->end_submit();
    }

    render_meshes_generation = backend->render_meshes_generation;
}

void RayTracingPipelineVulkan::dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) {
    VkStridedDeviceAddressRegionKHR callable_table = {};
    callable_table.deviceAddress = 0;

    vkrt::CmdTraceRaysKHR(render_cmd_buf,
        &shader_table.raygen,
        &shader_table.miss,
        &shader_table.hitgroup,
        &callable_table,
        width,
        height,
        batch_spp);
}
