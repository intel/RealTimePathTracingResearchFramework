// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "render_vulkan.h"

// default extensions
namespace vkrt {
    //void create_default_environment_extensions(std::vector<std::unique_ptr<RenderExtension>> &extensions, RenderVulkan* backend);
    void create_default_pointset_extensions(std::vector<std::unique_ptr<RenderExtension>> &extensions, RenderVulkan* backend);
    void create_default_light_sampling_extensions(std::vector<std::unique_ptr<RenderExtension>> &extensions, RenderVulkan* backend);
#ifdef ENABLE_DEBUG_VIEWS
    void create_default_debug_extensions(std::vector<std::unique_ptr<RenderExtension>> &extensions, RenderVulkan* backend);
#endif
} // namespace

std::vector<std::unique_ptr<RenderExtension>> RenderVulkan::create_default_extensions() {
    std::vector<std::unique_ptr<RenderExtension>> extensions;
    vkrt::create_default_pointset_extensions(extensions, this);
#ifdef ENABLE_DEBUG_VIEWS
    vkrt::create_default_debug_extensions(extensions, this);
#endif
    vkrt::create_default_light_sampling_extensions(extensions, this);
    return extensions;
}

struct ProcessTAAVulkan;
struct ProcessExampleVulkan;
struct ProcessUberPostVulkan;
struct ProcessProfilingToolsVulkan;
struct ProcessDepthOfField;
struct ProcessOIDN2DenoisingVulkan;
struct ProcessDLDenoisingVulkan;
struct ProcessDebugViewsVulkan;
struct ProcessReStirVulkan;

std::unique_ptr<RenderExtension> RenderVulkan::create_processing_step(RenderProcessingStep step) {
    switch (step) {
    case RenderProcessingStep::TAA:
        return create_render_extension<ProcessTAAVulkan>(this);
#ifdef ENABLE_EXAMPLES
    case RenderProcessingStep::Example:
        return create_render_extension<ProcessExampleVulkan>(this);
#endif
#ifdef ENABLE_POST_PROCESSING
    case RenderProcessingStep::UberPost:
        return create_render_extension<ProcessUberPostVulkan>(this);
    case RenderProcessingStep::DepthOfField:
        return create_render_extension<ProcessDepthOfField>(this);
#endif
#ifdef ENABLE_PROFILING_TOOLS
    case RenderProcessingStep::ProfilingTools:
        return create_render_extension<ProcessProfilingToolsVulkan>(this);
#endif
#ifdef ENABLE_OIDN
    case RenderProcessingStep::DLDenoising:
        return create_render_extension<ProcessDLDenoisingVulkan>(this);
#endif
#ifdef ENABLE_OIDN2
    case RenderProcessingStep::OIDN2:
        return create_render_extension<ProcessOIDN2DenoisingVulkan>(this);
#endif
    default:
        // unknown
        return RenderBackend::create_processing_step(step);
    }
}

#include "vkdisplay.h"

void VKDisplay::display(RenderGraphic *renderer) {
    if (auto* render_vk = dynamic_cast<RenderVulkan*>(renderer))
        display_native(render_vk->render_target());
    else
        Display::display(renderer);
}

RenderBackend* create_vulkan_backend(Display& display) {
    if (VKDisplay* vkdisplay = dynamic_cast<VKDisplay*>(&display))
        return new RenderVulkan(vkdisplay->device);
    else
        return new RenderVulkan(vkrt::Device());
}
