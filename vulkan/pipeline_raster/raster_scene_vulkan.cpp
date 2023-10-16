// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "../render_vulkan.h"
#include "raster_scene_vulkan.h"
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

namespace glsl {
    using namespace glm;
    #include "../../rendering/language.hpp"
    #include "../gpu_params.glsl"
}

#define USE_DEPTH_BUFFER

RasterScenePipelineVulkan::RasterScenePipelineVulkan(RenderVulkan* backend, GpuProgram const* program
    , vkrt::RenderPipelineOptions const& pipeline_options
    , bool defer)
    : RenderPipelineVulkan(backend, pipeline_options) {
    // this->gpu_program_features = program->feature_flags;
    this->pipeline_bindpoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    try {
        auto pipeline_build = std::make_unique<PendingBuild>();
        build_shader_descriptor_table(nullptr, VK_NULL_HANDLE
            , &pipeline_build->framebuffer_formats, &pipeline_build->framebuffer_depth_format);
        build_layout();
        build_pipeline(std::move(pipeline_build), program, defer);
    }
    catch (...) {
        internal_release_resources();
        throw;
    }
}

RasterScenePipelineVulkan::~RasterScenePipelineVulkan() {
    internal_release_resources();
}

void RasterScenePipelineVulkan::internal_release_resources() {
    pipeline_handle = VK_NULL_HANDLE;

    for (auto& rp : raster_pipeline_store) {
        vkDestroyPipeline(device->logical_device(), rp.second, nullptr);
        rp.second = VK_NULL_HANDLE;
    }
    raster_pipeline_store.clear();
}

std::string RasterScenePipelineVulkan::name() {
    // todo: store the source names?
    return "Raster Scene Pipeline";
}

void RasterScenePipelineVulkan::wait_for_construction() {
    if (this->pending_build) {
        this->pipeline_handle = build_raster_pipelines(*this->pending_build);
        this->pending_build.reset();
    }
}

void RasterScenePipelineVulkan::build_layout() {
    VkDescriptorSetLayout descriptor_layouts[backend->MAX_DESC_SETS] = { desc_layout };
    uint32_t default_push_const_size = 0;
    int desc_set_count = backend->register_descriptor_sets(descriptor_layouts, default_push_const_size, this->pipeline_options);
    assert(desc_set_count <= backend->MAX_DESC_SETS);

    VkPushConstantRange push_constants[1] = {};
    push_constants[0].offset = 0;
    push_constants[0].size = sizeof(RenderMeshParams);
    push_constants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkPipelineLayoutCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_create_info.setLayoutCount = desc_set_count;
    pipeline_create_info.pSetLayouts = descriptor_layouts;
    pipeline_create_info.pPushConstantRanges = push_constants;
    pipeline_create_info.pushConstantRangeCount = sizeof(push_constants) / sizeof(push_constants[0]);

    CHECK_VULKAN(vkCreatePipelineLayout(
        device->logical_device(), &pipeline_create_info, nullptr, &pipeline_layout));
}

VkPipeline RasterScenePipelineVulkan::build_raster_pipelines(PendingBuild const& pipeline_build) {
    VkVertexInputBindingDescription binding_desc[4] = {};
    VkVertexInputAttributeDescription attribute_desc[4] = {};
    int vac = 0;
    int vbc = 0;

    // positions
#ifdef QUANTIZED_POSITIONS
    attribute_desc[vac].format = VK_FORMAT_R32G32_UINT;
#define RASTER_VERTEX_STRIDE (sizeof(uint64_t))
    binding_desc[vbc].stride = RASTER_VERTEX_STRIDE;
    binding_desc[vbc].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
#else
    attribute_desc[vac].format = VK_FORMAT_R32G32B32_SFLOAT;
#define RASTER_VERTEX_STRIDE (sizeof(float) * 3)
    binding_desc[vbc].stride = RASTER_VERTEX_STRIDE;
    binding_desc[vbc].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
#endif
    attribute_desc[vac].binding = binding_desc[vbc++].binding = 0; // host bindpoint
    attribute_desc[vac++].location = 0; // bind to glsl

    // normals
#ifdef QUANTIZED_NORMALS_AND_UVS
    attribute_desc[vac].format = VK_FORMAT_R32_UINT;
    attribute_desc[vac].offset = 0;
#define RASTER_NORMAL_STRIDE (sizeof(uint32_t) * 2)
    binding_desc[vbc].stride = RASTER_NORMAL_STRIDE;
    binding_desc[vbc].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
#else
    attribute_desc[vac].format = VK_FORMAT_R32G32B32_SFLOAT;
#define RASTER_NORMAL_STRIDE (sizeof(float) * 3)
    binding_desc[vbc].stride = RASTER_NORMAL_STRIDE;
    binding_desc[vbc].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
#endif
    attribute_desc[vac].binding = binding_desc[vbc++].binding = 1; // host bindpoint
    attribute_desc[vac++].location = 1; // bind to glsl

    // uv
#ifdef QUANTIZED_NORMALS_AND_UVS
    attribute_desc[vac].format = VK_FORMAT_R32_UINT;
#define RASTER_UV_STRIDE RASTER_NORMAL_STRIDE
    attribute_desc[vac].offset = sizeof(uint32_t);
    attribute_desc[vac].binding = attribute_desc[vac-1].binding; // bind to previous bindpoint
#else
    attribute_desc[vac].format = VK_FORMAT_R32G32_SFLOAT;
#define RASTER_UV_STRIDE (sizeof(float) * 2)
    binding_desc[vbc].stride = RASTER_UV_STRIDE;
    binding_desc[vbc].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    binding_desc[vbc].binding = vbc;
    attribute_desc[vac].binding = binding_desc[vbc++].binding = 2; // host bindpoint
#endif
    attribute_desc[vac++].location = 2; // bind to glsl

    // instance
    attribute_desc[vac].format = VK_FORMAT_R32_UINT;
    binding_desc[vbc].stride = sizeof(uint32_t);
    binding_desc[vbc].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    attribute_desc[vac].binding = binding_desc[vbc++].binding = 4; // host bindpoint
    attribute_desc[vac++].location = 4; // bind to glsl

    VkPipelineVertexInputStateCreateInfo vertex_info = {};
    vertex_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_info.vertexBindingDescriptionCount = vbc;
    vertex_info.pVertexBindingDescriptions = binding_desc;
    vertex_info.vertexAttributeDescriptionCount = vac;
    vertex_info.pVertexAttributeDescriptions = attribute_desc;

    VkPipelineInputAssemblyStateCreateInfo ia_info = {};
    ia_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewport_info = {};
    viewport_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_info.viewportCount = 1;
    viewport_info.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster_info = {};
    raster_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_info.polygonMode = VK_POLYGON_MODE_FILL;
    raster_info.cullMode = VK_CULL_MODE_NONE;
    raster_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster_info.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms_info = {};
    ms_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineRenderingCreateInfoKHR fb_info = {};
    fb_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;

    VkPipelineColorBlendAttachmentState color_attachment[vkrt::BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS] = {};
    fb_info.pColorAttachmentFormats = pipeline_build.framebuffer_formats;
    while (fb_info.colorAttachmentCount < uint32_t(vkrt::BindingLayoutCollector::MAX_FRAMEBUFFER_BINDINGS)
        && pipeline_build.framebuffer_formats[fb_info.colorAttachmentCount] != VK_FORMAT_UNDEFINED) {
        int i = fb_info.colorAttachmentCount++;
        // blend options for each valid attachment
        color_attachment[i].blendEnable = VK_FALSE; // todo: add a pipeline option
        color_attachment[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo blend_info = {};
    blend_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend_info.attachmentCount = fb_info.colorAttachmentCount;
    blend_info.pAttachments = color_attachment;

    VkPipelineDepthStencilStateCreateInfo depth_info = {};
    depth_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
#ifdef USE_DEPTH_BUFFER
    fb_info.depthAttachmentFormat = pipeline_build.framebuffer_depth_format;
    fb_info.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
    if (pipeline_build.framebuffer_depth_format != VK_FORMAT_UNDEFINED) {
        depth_info.depthWriteEnable = this->pipeline_options.raster_depth;
        depth_info.depthTestEnable = this->pipeline_options.raster_depth || this->pipeline_options.depth_test;
        depth_info.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    }
#endif
    depth_info.minDepthBounds = 0.0f;
    depth_info.maxDepthBounds = 1.0f;

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]);
    dynamic_state.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pVertexInputState = &vertex_info;
    info.pInputAssemblyState = &ia_info;
    info.pViewportState = &viewport_info;
    info.pRasterizationState = &raster_info;
    info.pMultisampleState = &ms_info;
    info.pDepthStencilState = &depth_info;
    info.pColorBlendState = &blend_info;
    info.pDynamicState = &dynamic_state;
    info.layout = pipeline_layout;
    info.pNext = &fb_info;

    std::vector<VkGraphicsPipelineCreateInfo> create_infos;
    std::vector<VkPipelineShaderStageCreateInfo> stage_create_infos(pipeline_build.modules.size() * 2);
    int stage_offset = 0;
    for (auto const& shader_group : pipeline_build.modules) {
        auto& vertex_stage = stage_create_infos[stage_offset++];
        vertex_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertex_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertex_stage.module = shader_group.vertex;
        vertex_stage.pName = "main";
        auto& fragment_stage = stage_create_infos[stage_offset++];
        fragment_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragment_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragment_stage.module = shader_group.fragment;
        fragment_stage.pName = "main";

        auto create_info = info;
        create_info.stageCount = stage_create_infos.data() + stage_offset - &vertex_stage;
        create_info.pStages = &vertex_stage;
        create_infos.push_back(create_info);
    }

    std::vector<VkPipeline> raster_pipelines(create_infos.size());
    CHECK_VULKAN(vkCreateGraphicsPipelines(device->logical_device(), device->pipeline_cache()
        , uint_bound(create_infos.size()), create_infos.data()
        , nullptr, raster_pipelines.data()));

    this->pipeline_handle = raster_pipelines.front();
    this->raster_pipeline_store.elements.resize(raster_pipelines.size());
    for (int i = 0, ie = int_cast(raster_pipelines.size()); i < ie; ++i) {
        this->raster_pipeline_store.elements[i] = { pipeline_build.modules[i].name, raster_pipelines[i] };
    }

    return raster_pipelines.at(0);
}

bool RasterScenePipelineVulkan::build_pipeline(std::unique_ptr<PendingBuild> pipeline_build, GpuProgram const* program, bool defer) {
    make_gpu_program_binaries(program, pipeline_options);

// Todo: Some issue in the validation layers prevents us from doing the work asynchronously (version 1.3.211)
// Make sure to remove this when a new version comes out
#if defined(_DEBUG)
    defer = false;
#endif

    bool have_default_hit = false;
    for (int module_idx = 0; program->modules[module_idx]; ++module_idx) {
        auto module = program->modules[module_idx];
        auto vertex_unit = gpu_module_single_unit_typed(module, "vert");
        auto fragment_unit = gpu_module_single_unit_typed(module, "frag");

        PendingBuild::ShaderGroup group;
        group.name = module->name;
        group.name += "group";
        have_default_hit |= group.name == "basicgroup";

        group.vertex = vkrt::ShaderModule(*device
            , read_gpu_shader_binary(vertex_unit, pipeline_options));
        group.fragment = vkrt::ShaderModule(*device
            , read_gpu_shader_binary(fragment_unit, pipeline_options));

        pipeline_build->modules.push_back(group);
    }
    if (!have_default_hit)
        warning("Raster Pipeline %s does not contain a default group named 'basicgroup' (no vertex shader named 'basic')", name().c_str());

    if (!defer)
        this->pipeline_handle = build_raster_pipelines(*pipeline_build);
    else
        this->pending_build = std::move(pipeline_build);
    return !defer;
}

void RasterScenePipelineVulkan::build_shader_binding_table() {
    auto const& render_meshes = backend->render_meshes;

    this->raster_pipeline_table.clear();
    for (size_t i = 0; i < backend->parameterized_meshes.size(); ++i) {
        std::vector<RenderMeshParams> const &hit_group_params = render_meshes[i];
        const auto &shader_names = backend->shader_names[i];
        for (size_t j = 0; j < hit_group_params.size(); ++j) {
            // todo: raster equivalent with blending etc.
            // bool no_alpha = (hit_group_params[j].flags & GEOMETRY_FLAGS_NOALPHA) != 0;
            std::string hg_name;
            if (j < shader_names.size())
                hg_name = shader_names[j];
            if (hg_name.empty() || hg_name[0] == '+')
                hg_name.insert(0, "basic");
            hg_name += "group";

            this->raster_pipeline_table.push_back(this->raster_pipeline_store[hg_name]);
        }
    }

    unique_scene_id = backend->unique_scene_id;
    parameterized_meshes_revision = backend->parameterized_meshes_revision;
}

void RasterScenePipelineVulkan::update_shader_binding_table() {
    if (unique_scene_id != backend->unique_scene_id
     || parameterized_meshes_revision != backend->parameterized_meshes_revision) {
        this->build_shader_binding_table();
    }

    // nothing to do, command buffers are recorded per frame
    meshes_revision = backend->meshes_revision;
}

void RasterScenePipelineVulkan::update_shader_descriptor_table(vkrt::DescriptorSetUpdater& updater, int swap_index
    , CustomPipelineExtensionVulkan* optional_managing_extension) {
    vkrt::Texture2D framebuffer_targets[vkrt::BindingCollector::MAX_FRAMEBUFFER_BINDINGS];
    vkrt::BindingCollector blc = { updater, framebuffer_targets, this->framebuffer_depth_target };

    backend->update_shader_descriptor_table(blc, this->pipeline_options, desc_sets[swap_index]);

    this->framebuffer_targets.clear();
    for (int i = 0; i < vkrt::BindingCollector::MAX_FRAMEBUFFER_BINDINGS && framebuffer_targets[i]; ++i)
        this->framebuffer_targets.push_back(framebuffer_targets[i]);
}

void RasterScenePipelineVulkan::record_raster_commands(VkCommandBuffer render_cmd_buf) {
    VkPipeline currentPipeline = this->pipeline_handle;

    int totalInstanceCount = 0;
    for (int pm_idx = 0, pm_idx_end = int_cast(backend->parameterized_instances.size()); pm_idx < pm_idx_end; ++pm_idx) {
        auto const& pmi = backend->parameterized_instances[pm_idx];
        int instanceCount = int_cast(pmi.size());
        if (instanceCount <= 0) continue;

        auto const& pm = backend->parameterized_meshes[pm_idx];
        auto const& mesh_params = backend->render_meshes[pm_idx];
        auto const& mesh = backend->meshes[pm.mesh_id];

        VkBuffer instanceBuffers[] = { backend->parameterized_instance_buf };
        VkDeviceSize instanceOffsets[] = { totalInstanceCount * sizeof(uint32_t) };
        vkCmdBindVertexBuffers(render_cmd_buf, 4, 1, instanceBuffers, instanceOffsets);

        // all geometries of the current instanced mesh
        for (int j = 0, je = int_cast(mesh->geometries.size()); j < je; ++j) {
            auto const& hit_group_params = mesh_params[j];
            auto targetStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            vkCmdPushConstants(render_cmd_buf, pipeline_layout, targetStages, 0, sizeof(hit_group_params), &hit_group_params);

            auto meshPipeline = this->raster_pipeline_table[pm.render_mesh_base_offset + j];
            if (meshPipeline != currentPipeline) {
                vkCmdBindPipeline(
                    render_cmd_buf, pipeline_bindpoint, meshPipeline);
                currentPipeline = meshPipeline;
            }

            auto const& geom = mesh->geometries[j];

            VkBuffer vertexBuffers[] = { geom.vertex_buf, geom.normal_buf, geom.uv_buf };
            VkDeviceSize vertexOffsets[] = { geom.vertex_offset * RASTER_VERTEX_STRIDE, geom.vertex_offset * RASTER_NORMAL_STRIDE, geom.vertex_offset * RASTER_UV_STRIDE };
            vkCmdBindVertexBuffers(render_cmd_buf, 0, vertexBuffers[2] && vertexBuffers[2] != vertexBuffers[1] ? 3 : 2, vertexBuffers, vertexOffsets);

            bool use_indices = !geom.indices_are_implicit && geom.index_buf;
            if (use_indices) {
                vkCmdBindIndexBuffer(render_cmd_buf, geom.index_buf, geom.triangle_offset * 3 * sizeof(uint32_t), VK_INDEX_TYPE_UINT32);

                vkCmdDrawIndexed(render_cmd_buf, geom.num_triangles() * 3, instanceCount, 0, geom.index_offset, 0);
            }
            else
                vkCmdDraw(render_cmd_buf, geom.num_vertices(), instanceCount, 0, 0);
        }

        totalInstanceCount += instanceCount;
    }

    if (this->pipeline_handle != currentPipeline)
        vkCmdBindPipeline(
            render_cmd_buf, pipeline_bindpoint, this->pipeline_handle);
}

namespace vkrt {
    extern PFN_vkCmdBeginRenderingKHR CmdBeginRenderingKHR;
    extern PFN_vkCmdEndRenderingKHR CmdEndRenderingKHR;
}

void RasterScenePipelineVulkan::dispatch_rays(VkCommandBuffer render_cmd_buf, int width, int height, int batch_spp) {
    VkViewport viewport { };
    VkRect2D scissor = { };
    viewport.width = scissor.extent.width = width;
    viewport.height = scissor.extent.height = height;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(render_cmd_buf, 0, 1, &viewport);
    vkCmdSetScissor(render_cmd_buf, 0, 1, &scissor);

    VkRenderingInfoKHR renderingInfo = { };
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderingInfo.renderArea.extent.width = width;
    renderingInfo.renderArea.extent.height = height;
    renderingInfo.layerCount = 1;

    vkrt::MemoryBarriers<1, vkrt::BindingCollector::MAX_FRAMEBUFFER_BINDINGS + 1> barriers;

    VkRenderingAttachmentInfo color_info[vkrt::BindingCollector::MAX_FRAMEBUFFER_BINDINGS] = { };
    for (int i = 0, ie = int_cast(this->framebuffer_targets.size()); i < ie; ++i) {
        color_info[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_info[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        color_info[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_info[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_info[i].imageView = this->framebuffer_targets[i]->view_handle();

        barriers.add(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            , this->framebuffer_targets[i]->transition_color(color_info[i].imageLayout));
    }
    renderingInfo.pColorAttachments = color_info;
    renderingInfo.colorAttachmentCount = uint_bound(this->framebuffer_targets.size());

    VkRenderingAttachmentInfo depth_info = { };
    depth_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
#ifdef USE_DEPTH_BUFFER
    if (this->framebuffer_depth_target) {
        depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_info.imageView = this->framebuffer_depth_target->view_handle();
        depth_info.clearValue.depthStencil.depth = 1.0f;
        renderingInfo.pDepthAttachment = &depth_info;

        auto depth_barrier = this->framebuffer_depth_target->transition_color(depth_info.imageLayout);
        depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barriers.add(VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, depth_barrier);
    }
#endif

    VkClearColorValue clearColor = {0.0, 0.0, 0.0, 0.0};
    VkImageSubresourceRange imageRange = {};
    imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageRange.levelCount = 1;
    imageRange.layerCount = 1;

    for (int i = 0, ie = int_cast(this->framebuffer_targets.size()); i < ie; ++i)
        vkCmdClearColorImage(render_cmd_buf, this->framebuffer_targets[i]->image_handle(), VK_IMAGE_LAYOUT_GENERAL, &clearColor, 1, &imageRange);

    // synchronize and transition framebuffer to optimal layout
    barriers.set(render_cmd_buf, DEFAULT_IMAGEBUFFER_PIPELINE_STAGES);

    vkrt::CmdBeginRenderingKHR(render_cmd_buf, &renderingInfo);
    record_raster_commands(render_cmd_buf);
    vkrt::CmdEndRenderingKHR(render_cmd_buf);
}
