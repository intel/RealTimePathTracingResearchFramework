// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#include "vulkan_utils.h"
#include <array>
#include <iterator>
#include <stdexcept>
#include <cassert>
#include <vector>
#include <cstring>
#include <algorithm>
#include "error_io.h"
#include "util.h"
#include "types.h"

extern bool running_rendering_profiling;

#define ENABLE_SHADER_CLOCK
#define ENABLE_FLOAT32_ATOMICS
// #define ENABLE_FLOAT32_ADD_ATOMICS

//#define CACHE_ALL_HOST_MEMORY
//#define ENABLE_MEMORY_PRIORITIES
//#define MINIMIZE_DEVICE_LOCAL_MEMORY

#define USE_BLOCKED_ALLOCATION
#define MIN_ALLOCATION_BLOCK_SIZE_MB 2
#define ALLOCATION_BLOCK_SIZE_MB 24
#define COMMON_ALLOCATION_BLOCK_SIZE_MB 128
//#define FORCE_INDIVIDUAL_BLOCKS
//#define FORCE_SINGLE_ARENA

#if defined(ENABLE_CUDA) || defined(ENABLE_DPCPP)
#define ENABLE_EXTERNAL_MEMORY
#ifdef _WIN32
    #include <windows.h>
    #include <vulkan/vulkan_win32.h>
#endif
#endif

namespace vkrt {

// required, if raster is not enabled, otherwise optional
static const std::vector<const char *> ray_tracing_device_extensions = {
    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
    VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
    VK_KHR_RAY_QUERY_EXTENSION_NAME,
    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
};

static const std::vector<const char *> required_device_extensions = {
#ifdef ENABLE_FLOAT32_ATOMICS
    VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME,
#endif
#ifdef ENABLE_SHADER_CLOCK
    VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
#endif
#ifdef ENABLE_CMM
    VK_NV_COOPERATIVE_MATRIX_EXTENSION_NAME,
    VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,
#endif
#ifdef ENABLE_MEMORY_PRIORITIES
    VK_EXT_MEMORY_PRIORITY_EXTENSION_NAME,
    VK_EXT_PAGEABLE_DEVICE_LOCAL_MEMORY_EXTENSION_NAME,
#endif
#ifdef ENABLE_EXTERNAL_MEMORY
#ifdef _WIN32
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
#else
    VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
#endif
#endif
#ifdef ENABLE_RASTER
    VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
#endif
    // VK_EXT_VALIDATION_CACHE_EXTENSION_NAME,
};

static const std::array<const char *, 1> validation_layers = {"VK_LAYER_KHRONOS_validation"};

// RT extensions
PFN_vkCmdTraceRaysKHR CmdTraceRaysKHR = nullptr;
PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR = nullptr;
PFN_vkGetRayTracingShaderGroupHandlesKHR GetRayTracingShaderGroupHandlesKHR = nullptr;
PFN_vkCmdWriteAccelerationStructuresPropertiesKHR CmdWriteAccelerationStructuresPropertiesKHR =
    nullptr;
PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR = nullptr;
PFN_vkCmdCopyAccelerationStructureKHR CmdCopyAccelerationStructureKHR = nullptr;
PFN_vkCreateRayTracingPipelinesKHR CreateRayTracingPipelinesKHR = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddressKHR =
    nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR = nullptr;
PFN_vkCreateDeferredOperationKHR CreateDeferredOperationKHR = nullptr;
PFN_vkDeferredOperationJoinKHR DeferredOperationJoinKHR = nullptr;
PFN_vkDestroyDeferredOperationKHR DestroyDeferredOperationKHR = nullptr;

// memory export extensions
#ifdef ENABLE_EXTERNAL_MEMORY
// exported memory types
const VkMemoryPropertyFlags exported_memory_type_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

#ifdef _WIN32
#define EXT_MEMORY_HANDLE VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
PFN_vkGetMemoryWin32HandleKHR GetMemoryWin32HandleKHR = nullptr;
#else
#define EXT_MEMORY_HANDLE VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
PFN_vkGetMemoryFdKHR GetMemoryFdKHR = nullptr;
#endif
#endif

// raster extensions
#ifdef ENABLE_RASTER
PFN_vkCmdBeginRenderingKHR CmdBeginRenderingKHR = nullptr;
PFN_vkCmdEndRenderingKHR CmdEndRenderingKHR = nullptr;
#endif

void load_khr_ray_tracing(VkDevice &device)
{
    CmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(
        vkGetDeviceProcAddr(device, "vkCmdTraceRaysKHR"));
    DestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR"));
    GetRayTracingShaderGroupHandlesKHR =
        reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(
            vkGetDeviceProcAddr(device, "vkGetRayTracingShaderGroupHandlesKHR"));
    CmdWriteAccelerationStructuresPropertiesKHR =
        reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(
            vkGetDeviceProcAddr(device, "vkCmdWriteAccelerationStructuresPropertiesKHR"));
    CreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR"));
    CmdBuildAccelerationStructuresKHR =
        reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
            vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR"));
    CmdCopyAccelerationStructureKHR = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device, "vkCmdCopyAccelerationStructureKHR"));
    CreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(
        vkGetDeviceProcAddr(device, "vkCreateRayTracingPipelinesKHR"));
    GetAccelerationStructureDeviceAddressKHR =
        reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR"));
    GetAccelerationStructureBuildSizesKHR =
        reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
            vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR"));
    CreateDeferredOperationKHR =
        reinterpret_cast<PFN_vkCreateDeferredOperationKHR>(
            vkGetDeviceProcAddr(device, "vkCreateDeferredOperationKHR"));
    DeferredOperationJoinKHR =
        reinterpret_cast<PFN_vkDeferredOperationJoinKHR>(
            vkGetDeviceProcAddr(device, "vkDeferredOperationJoinKHR"));
    DestroyDeferredOperationKHR =
        reinterpret_cast<PFN_vkDestroyDeferredOperationKHR>(
            vkGetDeviceProcAddr(device, "vkDestroyDeferredOperationKHR"));
}

PFN_vkCreateValidationCacheEXT CreateValidationCacheEXT = nullptr;
PFN_vkDestroyValidationCacheEXT DestroyValidationCacheEXT = nullptr;
PFN_vkGetValidationCacheDataEXT GetValidationCacheDataEXT = nullptr;

PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV GetPhysicalDeviceCooperativeMatrixPropertiesNV = nullptr;

static char const pipeline_cache_file[] = "vulkan_cache";
static char const shader_cache_file[] = "vulkan_shader_cache";

#ifdef _DEBUG
static VkResult create_debug_utils_messenger_EXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger)
{
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

static void destroy_debug_utils_messenger_EXT(VkInstance instance,
                                              VkDebugUtilsMessengerEXT debugMessenger,
                                              const VkAllocationCallbacks *pAllocator)
{
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
               VkDebugUtilsMessageTypeFlagsEXT messageType,
               const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
               void *pUserData)
{
    switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        // not specified in instance creation flags to avoid overly verbose output
        println(LogLevel::VERBOSE, "%s", pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        println(LogLevel::INFORMATION, "%s", pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        if (pCallbackData->messageIdNumber == 0x4dae5635) // transfer source/dest optimal
            break;
        println(LogLevel::WARNING, "%s", pCallbackData->pMessage);
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        throw_error("%s", pCallbackData->pMessage);
        break;
    default:
        break;
    }

    return VK_FALSE;
}
#endif

Device::Device(const std::vector<std::string> &instance_extensions,
               const std::vector<std::string> &logical_device_extensions,
               const char *device_override)
{
    try { // need to handle all exceptions from here for manual multi-resource cleanup!
    make_instance(instance_extensions);
    
    select_physical_device(device_override);
    make_logical_device(logical_device_extensions);

    load_khr_ray_tracing(device);

    CreateValidationCacheEXT = reinterpret_cast<PFN_vkCreateValidationCacheEXT>(
            vkGetDeviceProcAddr(device, "vkCreateValidationCacheEXT"));
    DestroyValidationCacheEXT = reinterpret_cast<PFN_vkDestroyValidationCacheEXT>(
            vkGetDeviceProcAddr(device, "vkDestroyValidationCacheEXT"));
    GetValidationCacheDataEXT = reinterpret_cast<PFN_vkGetValidationCacheDataEXT>(
            vkGetDeviceProcAddr(device, "vkGetValidationCacheDataEXT"));

#ifdef ENABLE_EXTERNAL_MEMORY
#ifdef _WIN32
    GetMemoryWin32HandleKHR = reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
            vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR"));
#else
    GetMemoryFdKHR = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
            vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR"));
#endif
#endif

#ifdef ENABLE_CMM
    GetPhysicalDeviceCooperativeMatrixPropertiesNV = reinterpret_cast<PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesNV>(
            vkGetInstanceProcAddr(ref_data->vk_instance, "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV"));
#endif

#ifdef ENABLE_RASTER
    CmdBeginRenderingKHR = reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(
            vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
    CmdEndRenderingKHR = reinterpret_cast<PFN_vkCmdEndRenderingKHR>(
            vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));
#endif

    // Query the properties we'll use frequently
    vkGetPhysicalDeviceMemoryProperties(ref_data->vk_physical_device, &ref_data->mem_props);
    size_t totalDeviceMemory = 0;
    size_t totalVisibleDeviceMemory = 0;
    for (uint32_t i = 0; i < ref_data->mem_props.memoryHeapCount; ++i) {
        if (ref_data->mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            totalDeviceMemory += ref_data->mem_props.memoryHeaps[i].size;
    }
    for (uint32_t i = 0; i < ref_data->mem_props.memoryTypeCount; ++i) {
        ref_data->memory_type_is_device[i] = (ref_data->mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        if (ref_data->memory_type_is_device[i] && (ref_data->mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            totalVisibleDeviceMemory = std::max((size_t) ref_data->mem_props.memoryHeaps[ref_data->mem_props.memoryTypes[i].heapIndex].size, totalVisibleDeviceMemory);
    }
    size_t allocationBlockBase = totalDeviceMemory / ref_data->maxAllocationBlockCount;
    ref_data->allocationBlockSize = decltype(ref_data->allocationBlockSize)( std::max(allocationBlockBase, size_t(ALLOCATION_BLOCK_SIZE_MB * 1024 * 1024)) );
    ref_data->commonAllocationBlockSize = decltype(ref_data->commonAllocationBlockSize)( std::max(allocationBlockBase, size_t(COMMON_ALLOCATION_BLOCK_SIZE_MB * 1024 * 1024)) );
    ref_data->minAllocationBlockSize = MIN_ALLOCATION_BLOCK_SIZE_MB * 1024 * 1024;
    ref_data->maxAllocationBlockSize = COMMON_ALLOCATION_BLOCK_SIZE_MB * 1024 * 1024;
    println(CLL::INFORMATION, "Device memory: %sB (visible %sB), block sizes (%sB, %sB)"
        , pretty_print_count((double) totalDeviceMemory).c_str()
        , pretty_print_count((double) totalVisibleDeviceMemory).c_str()
        , pretty_print_count((double) ref_data->allocationBlockSize).c_str()
        , pretty_print_count((double) ref_data->commonAllocationBlockSize).c_str());

    ref_data->memory_arenas.reserve(32);
    ref_data->memory_arenas.resize(DefaultArenaCount);

    if (ref_data->ray_tracing_extension)
    {
        VkPhysicalDeviceProperties2 props = {};
        props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        ref_data->as_props.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
        ref_data->as_props.pNext = props.pNext;
        props.pNext = &ref_data->as_props;
        ref_data->rt_pipeline_props.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
        ref_data->rt_pipeline_props.pNext = props.pNext;
        props.pNext = &ref_data->rt_pipeline_props;
        vkGetPhysicalDeviceProperties2(ref_data->vk_physical_device, &props);

        println(CLL::VERBOSE, "Max #primitives = %lld, #instances = %lld, #geometries = %lld"
            , (long long) ref_data->as_props.maxPrimitiveCount
            , (long long) ref_data->as_props.maxInstanceCount
            , (long long) ref_data->as_props.maxGeometryCount);
    }

#ifdef ENABLE_CMM
    {
        uint32_t cmmPropCount = 0;
        GetPhysicalDeviceCooperativeMatrixPropertiesNV(ref_data->vk_physical_device, &cmmPropCount, nullptr);
        std::vector<VkCooperativeMatrixPropertiesNV> cmmProperties(cmmPropCount);
        for (auto& cmm : cmmProperties)
            cmm.sType = VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_NV;
        GetPhysicalDeviceCooperativeMatrixPropertiesNV(ref_data->vk_physical_device, &cmmPropCount, cmmProperties.data());
        println(CLL::VERBOSE, "Supported CMM sizes:");
        for (auto& cmm : cmmProperties) {
            if (cmm.scope != VK_SCOPE_SUBGROUP_NV) continue;
            auto type_char = [](VkComponentTypeNV ctype) -> char const* {
                if (ctype == VK_COMPONENT_TYPE_SINT8_NV)
                    return "i8";
                else if (ctype == VK_COMPONENT_TYPE_SINT16_NV)
                    return "i16";
                else if (ctype == VK_COMPONENT_TYPE_SINT32_NV)
                    return "i32";
                else if (ctype == VK_COMPONENT_TYPE_FLOAT16_NV)
                    return "h";
                else if (ctype == VK_COMPONENT_TYPE_FLOAT32_NV)
                    return "f";
                return nullptr;
            };
            if (!type_char(cmm.AType)) continue;
            if (!type_char(cmm.BType)) continue;
            if (!type_char(cmm.CType)) continue;
            if (!type_char(cmm.DType)) continue;
            println(CLL::VERBOSE, "%dx%d%s * %dx%d%s + %dx%d%s = %dx%d%s"
                , (int) cmm.MSize, (int) cmm.KSize, type_char(cmm.AType)
                , (int) cmm.KSize, (int) cmm.NSize, type_char(cmm.BType)
                , (int) cmm.MSize, (int) cmm.NSize, type_char(cmm.CType)
                , (int) cmm.MSize, (int) cmm.NSize, type_char(cmm.DType));
        }
    }
    {
        VkPhysicalDeviceSubgroupSizeControlProperties subgroup_props = {};
        subgroup_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_PROPERTIES;
        VkPhysicalDeviceProperties2 props2 = {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &subgroup_props;
        vkGetPhysicalDeviceProperties2(ref_data->vk_physical_device, &props2);
        println(CLL::VERBOSE, "Subgroup size [%d, %d]"
                , (int) subgroup_props.minSubgroupSize, (int) subgroup_props.maxSubgroupSize);
        // todo: currently Intel only supports SG=8 for CMM
        if (subgroup_props.minSubgroupSize != subgroup_props.maxSubgroupSize)
            ref_data->subgroupSize = subgroup_props.minSubgroupSize;
    }
#endif

    {
        std::vector<uint8_t> cache_data;
        FILE* cache_file = fopen(binary_path(pipeline_cache_file).c_str(), "rb");
        if (cache_file) {
            fseek(cache_file, 0, SEEK_END);
            size_t cache_size = int_cast<size_t>(ftell(cache_file));
            fseek(cache_file, 0, SEEK_SET);
            cache_data.resize(cache_size);
            if (fread(cache_data.data(), 1, cache_size, cache_file) != cache_size)
                cache_data.clear();
            fclose(cache_file);
        }

        VkPipelineCacheCreateInfo info = { };
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        info.initialDataSize = cache_data.size();
        info.pInitialData = cache_data.data();
        vkCreatePipelineCache(device, &info, nullptr, &ref_data->pipeline_cache);
    }
    if (CreateValidationCacheEXT)
    {
        std::vector<uint8_t> cache_data;
        FILE* cache_file = fopen(binary_path(shader_cache_file).c_str(), "rb");
        if (cache_file) {
            fseek(cache_file, 0, SEEK_END);
            size_t cache_size = int_cast<size_t>(ftell(cache_file));
            fseek(cache_file, 0, SEEK_SET);
            cache_data.resize(cache_size);
            if (fread(cache_data.data(), 1, cache_size, cache_file) != cache_size)
                cache_data.clear();
            fclose(cache_file);
        }

        VkValidationCacheCreateInfoEXT info = { };
        info.sType = VK_STRUCTURE_TYPE_VALIDATION_CACHE_CREATE_INFO_EXT;
        info.initialDataSize = cache_data.size();
        info.pInitialData = cache_data.data();
        CreateValidationCacheEXT(device, &info, nullptr, &ref_data->validation_cache);
    }

    // mult-resource management cleanup
    } catch (...) {
        release_resources();
        throw;
    }
}

void Device::release_resources()
{
    if (ref_data->vk_instance != VK_NULL_HANDLE) {
        delete ref_data->secondary_async_commands;
        delete ref_data->secondary_sync_commands;
        delete ref_data->main_async_commands;
        delete ref_data->main_sync_commands;
#ifdef _DEBUG
        destroy_debug_utils_messenger_EXT(ref_data->vk_instance, ref_data->debug_messenger, nullptr);
#endif
        if (ref_data->pipeline_cache)
            vkDestroyPipelineCache(device, ref_data->pipeline_cache, nullptr);
        if (ref_data->validation_cache)
            DestroyValidationCacheEXT(device, ref_data->validation_cache, nullptr);

        vkDestroyDevice(device, nullptr);
        vkDestroyInstance(ref_data->vk_instance, nullptr);
        ref_data->vk_instance = VK_NULL_HANDLE;

        println(CLL::INFORMATION, "Created a total number of %u buffers and %u images."
            , (unsigned) ref_data->mem_stats.total_buffers_created
            , (unsigned) ref_data->mem_stats.total_images_created);
        println(CLL::INFORMATION, "Allocated %u blocks totalling a maximum of %sB. Leaked %sB."
            , (unsigned) ref_data->mem_stats.total_allocation_count
            ,  pretty_print_count(ref_data->mem_stats.max_bytes_allocated).c_str()
            ,  pretty_print_count(ref_data->mem_stats.bytes_currently_allocated).c_str());
    }
}

Device::~Device()
{
    this->discard_reference();
}

VkPhysicalDevice Device::physical_device() const
{
    return ref_data->vk_physical_device;
}

VkInstance Device::instance()
{
    return ref_data->vk_instance;
}

VkQueue Device::main_queue()
{
    return ref_data->main_queue;
}

uint32_t Device::main_queue_index() const
{
    return ref_data->main_queue_index;
}

VkQueue Device::secondary_queue()
{
    return ref_data->secondary_queue;
}
uint32_t Device::secondary_queue_index() const
{
    return ref_data->secondary_queue_index;
}

CommandStream *Device::sync_command_stream(CommandQueueType type)
{
    if (type == CommandQueueType::Main)
    {
        if (!ref_data->main_sync_commands)
            ref_data->main_sync_commands = new SyncCommandStream(*this, CommandQueueType::Main);
        return ref_data->main_sync_commands;
    }
    else
    {
        if (!ref_data->secondary_sync_commands)
            ref_data->secondary_sync_commands = new SyncCommandStream(*this, CommandQueueType::Secondary);
        return ref_data->secondary_sync_commands;
    }
}

CommandStream *Device::async_command_stream(CommandQueueType type)
{
    if (type == CommandQueueType::Main) {
        if (!ref_data->main_async_commands)
            ref_data->main_async_commands = new AsyncCommandStream(*this, CommandQueueType::Main);
        return ref_data->main_async_commands;
    } else {
        if (!ref_data->secondary_async_commands)
            ref_data->secondary_async_commands = new AsyncCommandStream(*this, CommandQueueType::Secondary);
        return ref_data->secondary_async_commands;
    }
}

void Device::flush_sync_and_async_device_copies() {
    auto async_commands = async_command_stream();
    auto sync_commands = sync_command_stream();
    // make all data visible
    async_commands->begin_record();
    {
        VkMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(async_commands->current_buffer,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                            0,
                            1, &barrier,
                            0, nullptr,
                            0, nullptr);
    }
    async_commands->end_submit();
    async_commands->wait_complete();

    sync_commands->release_command_buffers();
    async_commands->release_command_buffers();

}

VkValidationCacheEXT Device::validation_cache() {
    return ref_data->validation_cache;
}
VkPipelineCache Device::pipeline_cache() {
    return ref_data->pipeline_cache;
}

void Device::update_pipeline_cache() {
    if (running_rendering_profiling)
        return;
    do
    {
        if (!ref_data->pipeline_cache)
            break;
        size_t cache_size = 0;
        vkGetPipelineCacheData(device, ref_data->pipeline_cache, &cache_size, nullptr);
        if (!cache_size)
            break;
        std::vector<uint8_t> cache_data(cache_size);
        if (VK_SUCCESS != vkGetPipelineCacheData(device, ref_data->pipeline_cache, &cache_size, cache_data.data())) {
            warning("Error retrieving pipeline cache data!");
            break;
        }
        cache_data.resize(cache_size);
        FILE* cache_file = fopen(binary_path(pipeline_cache_file).c_str(), "wb");
        if (cache_file) {
            fwrite(cache_data.data(), 1, cache_data.size(), cache_file);
            fclose(cache_file);
        }
    }
    while (false);
    do
    {
        if (!ref_data->validation_cache)
            break;
        size_t cache_size = 0;
        GetValidationCacheDataEXT(device, ref_data->validation_cache, &cache_size, nullptr);
        if (!cache_size)
            break;
        std::vector<uint8_t> cache_data(cache_size);
        if (VK_SUCCESS != GetValidationCacheDataEXT(device, ref_data->validation_cache, &cache_size, cache_data.data())) {
            warning("Error retrieving shader cache data!");
            break;
        }
        cache_data.resize(cache_size);
        FILE* cache_file = fopen(binary_path(shader_cache_file).c_str(), "wb");
        if (cache_file) {
            fwrite(cache_data.data(), 1, cache_data.size(), cache_file);
            fclose(cache_file);
        }
    }
    while (false);
}

VkCommandPool Device::make_command_pool(CommandQueueType type, VkCommandPoolCreateFlags flags)
{
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.flags = flags;
    create_info.queueFamilyIndex = type == CommandQueueType::Main ? ref_data->main_queue_index : ref_data->secondary_queue_index;
    CHECK_VULKAN(vkCreateCommandPool(device, &create_info, nullptr, &pool));
    return pool;
}

uint32_t Device::next_arena(size_t count) {
    uint32_t next_offset = ref_data->memory_arenas.size();
    ref_data->memory_arenas.resize(ref_data->memory_arenas.size() + count);
    return next_offset;
}
uint32_t Device::current_arena_index() const {
    uint32_t arena_count = (uint32_t) ref_data->memory_arenas.size();
    if (arena_count <= DefaultArenaCount)
        return PersistentArena;
    return arena_count - 1;
}

uint32_t Device::memory_type_index(uint32_t type_filter, VkMemoryPropertyFlags props) const
{
#ifdef MINIMIZE_DEVICE_LOCAL_MEMORY
    // avoid overflowing device-local heaps for non-local allocations
    if (!(props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        // note: forces matching flag bits for both device-local and host-cached flags to not accidentally add the latter
        uint32_t test_props = props | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        for (uint32_t i = 0; i < ref_data->mem_props.memoryTypeCount; ++i) {
            if (type_filter & (1 << i) &&
                (ref_data->mem_props.memoryTypes[i].propertyFlags & test_props) == props)
                return i;
        }
    }
#endif
    for (uint32_t i = 0; i < ref_data->mem_props.memoryTypeCount; ++i) {
        if (type_filter & (1 << i) &&
            (ref_data->mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("failed to find appropriate memory");
}

Device::Allocation Device::alloc(uint32_t arena, size_t nbytes
    , uint32_t type_filter, size_t alignment
    , VkMemoryPropertyFlags props, VkMemoryAllocateFlags allocFlags
    , size_t block_size_hint
    , float mem_priority)
{
#ifdef FORCE_SINGLE_ARENA
    arena = 0;
#endif

    Device::Allocation result = { };
    result.arena = decltype(result.arena)(arena);
    result.type = decltype(result.type)( memory_type_index(type_filter, props) );

    VkMemoryAllocateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = nbytes;
    info.memoryTypeIndex = result.type;

#ifdef USE_BLOCKED_ALLOCATION
    static const auto BLOCK_FULL_MARKER = decltype(MemoryArena::Block::cursor)(~0);
    size_t target_block_size = (block_size_hint ? block_size_hint : ref_data->allocationBlockSize);
    target_block_size = std::min(std::max(target_block_size, (size_t) ref_data->minAllocationBlockSize), (size_t) ref_data->maxAllocationBlockSize);
    // (cursor == BLOCK_FULL_MARKER) <=> (cursor == block size)
    assert(target_block_size <= BLOCK_FULL_MARKER);

    if (arena >= ref_data->memory_arenas.size())
        ref_data->memory_arenas.resize(arena + 1);
    auto& blocks = ref_data->memory_arenas[arena].types[result.type];

    // ensure ascending order w.r.t. remaining allocation size
    auto fix_block_order = [&blocks]() {
        auto block_link = &blocks.back();
        size_t now_remaining = block_link->cursor != BLOCK_FULL_MARKER ? block_link->size - block_link->cursor : 0;
        for (auto next_block_link = block_link; next_block_link-- != blocks.data(); block_link = next_block_link) {
            size_t next_remaining = next_block_link->size - next_block_link->cursor;
            if (next_block_link->cursor == BLOCK_FULL_MARKER || next_remaining == 0)
                break;
            if (next_remaining <= now_remaining)
                break;
            std::swap(*block_link, *next_block_link);
        }
    };

    // unknown alignment -> individual blocks
    if (alignment != 0 && !blocks.empty()) {
        auto block_begin = blocks.data(), block_end = block_begin + blocks.size();
        auto block_match = block_end;
        // find smallest fitting space (blocks are in ascending order)
        for (auto block_seek = block_end; block_seek-- != block_begin; ) {
            auto& block = *block_seek;
            if (block.cursor != BLOCK_FULL_MARKER &&
                align_to(block.cursor, alignment) + nbytes <= block.size) {
                block_match = block_seek;
            }
            else
                break;
        }
        if (block_match != block_end) {
            auto& block = *block_match;
            size_t next_offset = align_to(block.cursor, alignment);
            result.memory = block.memory;
            result.offset = next_offset;
            block.freed += decltype(block.freed)(next_offset - block.cursor); // note: alignment will be missing in free size
            block.cursor = decltype(block.cursor)(next_offset + nbytes); // note: all shared blocks need to be < 4 GB
            fix_block_order();
            return result;
        }
    }

    blocks.emplace_back();
    auto& block = blocks.back();
#ifndef FORCE_INDIVIDUAL_BLOCKS
    // unknown alignment -> individual blocks
    if (alignment != 0) {
        // make sure that each blocked allocation can share with at least one other
        if (nbytes <= target_block_size / 2)
            info.allocationSize = target_block_size;
        // otherwise, each allocation gets its own block to avoid waste
        else
            info.allocationSize = nbytes;
    }
#endif

    block.size = info.allocationSize;
#endif

    VkMemoryAllocateFlagsInfo flags = {};
    flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flags.flags = allocFlags;
    if (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        flags.flags |= VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    }
    if (flags.flags != 0) {
        flags.pNext = info.pNext;
        info.pNext = &flags;
    }

#ifdef ENABLE_EXTERNAL_MEMORY
    VkExportMemoryAllocateInfo exportInfo = {};
    if (props & exported_memory_type_properties) {
        exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
        exportInfo.handleTypes = EXT_MEMORY_HANDLE;
        exportInfo.pNext = info.pNext;
        info.pNext = &exportInfo;
    }
#endif
#ifdef ENABLE_MEMORY_PRIORITIES
    VkMemoryPriorityAllocateInfoEXT mem_prio_info = {};
    mem_prio_info.sType = VK_STRUCTURE_TYPE_MEMORY_PRIORITY_ALLOCATE_INFO_EXT;
    mem_prio_info.priority = mem_priority;
    mem_prio_info.pNext = info.pNext;
    info.pNext = &mem_prio_info;
#endif

    CHECK_VULKAN(vkAllocateMemory(device, &info, nullptr, &result.memory));

    auto& stats = ref_data->mem_stats;
    stats.total_bytes_allocated += info.allocationSize;
    stats.bytes_currently_allocated += info.allocationSize;
    stats.max_bytes_allocated = std::max(stats.bytes_currently_allocated, stats.max_bytes_allocated);
    if (ref_data->memory_type_is_device[result.type]) {
        stats.device_bytes_currently_allocated += info.allocationSize;
        stats.max_device_bytes_allocated = std::max(stats.device_bytes_currently_allocated, stats.max_device_bytes_allocated);
    }
    stats.total_allocation_count++;

#ifdef USE_BLOCKED_ALLOCATION
    block.memory = result.memory;
    if (nbytes > BLOCK_FULL_MARKER)
        block.cursor = BLOCK_FULL_MARKER; // special marker to avoid overflow if allocations >= 4 GB
    else
        block.cursor = decltype(block.cursor)(nbytes); // note: all shared blocks need to be < 4 GB
    fix_block_order();
#endif
    return result;
}

void Device::free(uint32_t arena, uint32_t type, VkDeviceMemory &memory, size_t allocSize)
{
#ifdef FORCE_SINGLE_ARENA
    arena = 0;
#endif
    if (!memory)
        return;
#ifdef USE_BLOCKED_ALLOCATION
    if (arena >= ref_data->memory_arenas.size())
        return;
    auto& blocks = ref_data->memory_arenas[arena].types[type];

    auto block_fwd = blocks.data();
    auto block_end = block_fwd + blocks.size();
    auto block_backward = block_end;
    auto block_link = block_backward;
    for (; block_fwd != block_backward--; ++block_fwd) {
        if (block_backward->memory == memory) {
            block_link = block_backward;
            break;
        }
        if (block_fwd == block_backward)
            break;
        if (block_fwd->memory == memory) {
            block_link = block_fwd;
            break;
        }
    }

    if (block_link != block_end) {
        auto& block = *block_link;

        size_t free_size = block.freed + allocSize;
        if (free_size != block.size && free_size != block.cursor) {
            assert(free_size < block.size);
            block.freed = decltype(block.freed)(free_size);
            memory = VK_NULL_HANDLE;
            return;
        }
        free_size = block.size;
#else
        size_t free_size = allocSize;
#endif
        ref_data->mem_stats.bytes_currently_allocated -= free_size;
        if (ref_data->memory_type_is_device[type])
            ref_data->mem_stats.device_bytes_currently_allocated -= free_size;

        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;

#ifdef USE_BLOCKED_ALLOCATION
        blocks.erase(blocks.begin() + (block_link - blocks.data()));
    }
#endif
}

std::vector<MemoryArena::Block> Device::blocks_in_arena(uint32_t arena, VkMemoryPropertyFlags props) const {
    std::vector<MemoryArena::Block> collected_blocks;
    if (arena >= ref_data->memory_arenas.size())
        return collected_blocks;
    auto& blocks_by_types = ref_data->memory_arenas[arena].types;
    for (uint32_t i = 0; i < ref_data->mem_props.memoryTypeCount; ++i) {
        if (ref_data->mem_props.memoryTypes[i].propertyFlags & props)
            collected_blocks.insert(collected_blocks.end(), blocks_by_types[i].begin(), blocks_by_types[i].end());
    }
    return collected_blocks;
}

size_t Device::num_blocks_in_arena(uint32_t arena, uint32_t type) const {
    if (arena >= ref_data->memory_arenas.size())
        return 0;
    auto& blocks_by_types = ref_data->memory_arenas[arena].types;
    if (type < VK_MAX_MEMORY_TYPES)
        return blocks_by_types[type].size();
    size_t count = 0;
    for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i)
        count += blocks_by_types[i].size();
    return count;
}

const VkPhysicalDeviceMemoryProperties &Device::memory_properties() const {
    return ref_data->mem_props;
}
const VkPhysicalDeviceAccelerationStructurePropertiesKHR& Device::acceleration_structure_properties() const {
    return ref_data->as_props;
}
const VkPhysicalDeviceRayTracingPipelinePropertiesKHR& Device::raytracing_pipeline_properties() const {
    return ref_data->rt_pipeline_props;
}
MemoryStatistics const& Device::memory_statistics() const {
    return ref_data->mem_stats;
}

void Device::make_instance(const std::vector<std::string> &extensions)
{
    VkApplicationInfo app_info = {};
    app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app_info.pApplicationName = "Real-time Path Tracing Research Framework";
    app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.pEngineName = "None";
    app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    app_info.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char *> extension_names;
    for (const auto &ext : extensions) {
        extension_names.push_back(ext.c_str());
    }

#ifdef _DEBUG
    extension_names.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif
    VkInstanceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pApplicationInfo = &app_info;
    create_info.enabledExtensionCount = extension_names.size();
    create_info.ppEnabledExtensionNames =
        extension_names.empty() ? nullptr : extension_names.data();
#ifdef _DEBUG
    create_info.enabledLayerCount = validation_layers.size();
    create_info.ppEnabledLayerNames = validation_layers.data();

    VkValidationFeatureEnableEXT enabled_validation_features[] = { VK_VALIDATION_FEATURE_ENABLE_DEBUG_PRINTF_EXT };
    VkValidationFeaturesEXT      validation_features = {};
    validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validation_features.enabledValidationFeatureCount  = sizeof(enabled_validation_features) / sizeof(enabled_validation_features[0]);
    validation_features.pEnabledValidationFeatures     = enabled_validation_features;
    validation_features.pNext                          = create_info.pNext;
    create_info.pNext = &validation_features;

    println(CLL::INFORMATION, "Enabling %d validation layer(s)", (int) validation_layers.size());
#else
    create_info.enabledLayerCount = 0;
    create_info.ppEnabledLayerNames = nullptr;
#endif

    CHECK_VULKAN(vkCreateInstance(&create_info, nullptr, &ref_data->vk_instance));

#ifdef _DEBUG
    VkDebugUtilsMessengerCreateInfoEXT debug_create_info = {};
    debug_create_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debug_create_info.messageSeverity =  // VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug_create_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug_create_info.pfnUserCallback = debug_callback;
    debug_create_info.pUserData = nullptr;

    CHECK_VULKAN(create_debug_utils_messenger_EXT(
        ref_data->vk_instance, &debug_create_info, nullptr, &ref_data->debug_messenger));
#endif
}

void Device::select_physical_device(const char *device_override)
{
    uint32_t device_count = 0;
    CHECK_VULKAN(vkEnumeratePhysicalDevices(ref_data->vk_instance, &device_count, nullptr));
    std::vector<VkPhysicalDevice> devices(device_count, VkPhysicalDevice{});
    CHECK_VULKAN(vkEnumeratePhysicalDevices(ref_data->vk_instance, &device_count, devices.data()));

    if (device_override) {
        println(CLL::INFORMATION, "Looking for device \"%s\"", device_override);
    }

    for (const auto &d : devices) {
        VkPhysicalDeviceProperties2 props2 = {};
        VkPhysicalDeviceProperties& properties = props2.properties;
        VkPhysicalDeviceDriverProperties driver_props = {};
        driver_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &driver_props;
        vkGetPhysicalDeviceProperties2(d, &props2);

        // find requested device, if given
        if (device_override && strcmp(device_override, properties.deviceName) != 0) {
            println(CLL::WARNING, "Ignoring non-requested device \"%s\"", properties.deviceName);
            continue;
        }

        uint32_t extension_count = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extension_count, nullptr);
        std::vector<VkExtensionProperties> extensions(extension_count,
                                                      VkExtensionProperties{});
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extension_count, extensions.data());

        char const* ray_tracing_ext = nullptr;
        char const* non_semantic_info_ext = nullptr;
        // Check for ray tracing support on this device. We need the acceleration structure
        // and ray pipeline extensions
        for (VkExtensionProperties const& e : extensions) {
            if (0 == std::strcmp(e.extensionName, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
                ray_tracing_ext = VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME;
            if (0 == std::strcmp(e.extensionName, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME))
                non_semantic_info_ext = VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME;
        }

        // todo: check presence of all user-requested logical extensions?
        // make sure we can create a swap chain etc.

        if (!ray_tracing_ext) {
            println(CLL::WARNING, "Found device \"%s\", but it does not support "
                "ray tracing.", properties.deviceName);
#ifndef ENABLE_RASTER
            continue;
#endif
        }

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures(d, &features);

#ifndef COMPILING_FOR_DG2
        static constexpr uint32_t intel_vendor_id = 0x8086u;
        if (properties.vendorID == intel_vendor_id) {
            print(CLL::WARNING, "Selecting an Intel GPU, but DG2 extensions "
                "are disabled in this build!\n");
        }
#endif

        ref_data->vk_physical_device = d;

        println(CLL::INFORMATION, "Device: %s", properties.deviceName);
        println(CLL::INFORMATION, "Driver: %s %s", driver_props.driverName,
            driver_props.driverInfo);
        ref_data->ray_tracing_extension = ray_tracing_ext;
#ifdef _DEBUG
        ref_data->non_semantic_info_extension = non_semantic_info_ext;
#endif
        ref_data->nanoseconds_per_tick = properties.limits.timestampPeriod;
        ref_data->maxAllocationBlockCount = properties.limits.maxMemoryAllocationCount;
        ref_data->nonCoherentAtomSize = properties.limits.nonCoherentAtomSize;
        ref_data->deviceBufferAlignment = (uint32_t) std::max(std::max(
            properties.limits.minUniformBufferOffsetAlignment,
            properties.limits.minStorageBufferOffsetAlignment),
            properties.limits.minTexelBufferOffsetAlignment );
        ref_data->hostBufferAtomSize = properties.limits.nonCoherentAtomSize;
        ref_data->hostBufferAlignment = std::max(ref_data->deviceBufferAlignment, ref_data->hostBufferAtomSize);

        println(CLL::INFORMATION, "Max bound descriptor sets: %u", properties.limits.maxBoundDescriptorSets);
        println(CLL::INFORMATION, "Max allocations: %u", properties.limits.maxMemoryAllocationCount);
        println(CLL::INFORMATION, "GLSL debugPrintfEXT() support detected, define:\n  %s\nand print to stdout in environment\n  %s",
            "#extension GL_EXT_debug_printf : enable",
            "export DEBUG_PRINTF_TO_STDOUT=1");

        // keep enumerating all devices in device override mode so it is easy to switch
        if (!device_override)
            break;
    }

    if (ref_data->vk_physical_device == VK_NULL_HANDLE)
        throw_error("Failed to find suitable GPU");
}

void Device::make_logical_device(const std::vector<std::string> &additional_extensions)
{
    // Get the number of queue families
    uint32_t num_queue_families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ref_data->vk_physical_device, &num_queue_families, nullptr);

    // Get the queue faùilies properties
    std::vector<VkQueueFamilyProperties> family_props(num_queue_families, VkQueueFamilyProperties{});
    vkGetPhysicalDeviceQueueFamilyProperties(ref_data->vk_physical_device, &num_queue_families, family_props.data());

    // Queue creation structures
    VkDeviceQueueCreateInfo queue_create_info[2] = {};

    // Let's find the main queue (graphics and compute)
    for (uint32_t i = 0; i < num_queue_families; ++i)
    {
        VkQueueFlags compute_family_bits = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((family_props[i].queueFlags & compute_family_bits) == compute_family_bits) {
            ref_data->main_queue_index = i;
            break;
        }
    }
    // Validate the main queue
    if (ref_data->main_queue_index == decltype(ref_data->main_queue_index)(~0))
        throw_error("No joint graphics & compute queue available");

    // Main queue create info
    const float main_queue_priority = 1.0f;
    queue_create_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info[0].queueFamilyIndex = ref_data->main_queue_index;
    queue_create_info[0].queueCount = 1;
    queue_create_info[0].pQueuePriorities = &main_queue_priority;

    // Let's find the secodnary queue (compute)
    for (uint32_t i = 0; i < num_queue_families; ++i)
    {
        VkQueueFlags compute_family_bits = VK_QUEUE_COMPUTE_BIT;
        if (i == ref_data->main_queue_index)
            continue;

        if ((family_props[i].queueFlags & compute_family_bits) == compute_family_bits) {
            ref_data->secondary_queue_index = i;
            break;
        }
    }
    // Validate the main queue
    bool secondaryQueue = ref_data->secondary_queue_index != decltype(ref_data->secondary_queue_index)(~0);
    if (secondaryQueue) {
        // Secondary queue create info
        const float secondary_queue_priority = 0.0f;
        queue_create_info[1].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_create_info[1].queueFamilyIndex = ref_data->secondary_queue_index;
        queue_create_info[1].queueCount = 1;
        queue_create_info[1].pQueuePriorities = &secondary_queue_priority;
    }

    VkPhysicalDeviceSynchronization2FeaturesKHR sync_features = {};
    sync_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
    sync_features.synchronization2 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features device_12_features = {};
    device_12_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    device_12_features.descriptorIndexing = VK_TRUE; // feature set for subseq.
    device_12_features.runtimeDescriptorArray = VK_TRUE;
    device_12_features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    device_12_features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE; // raise limits of textures
    device_12_features.descriptorBindingUpdateUnusedWhilePending = VK_TRUE; // overlayed update
    device_12_features.descriptorBindingPartiallyBound = VK_TRUE; // async update
    device_12_features.bufferDeviceAddress = VK_TRUE;
    device_12_features.timelineSemaphore = VK_TRUE;
    device_12_features.hostQueryReset = VK_TRUE;
    device_12_features.pNext = &sync_features;

    VkPhysicalDeviceVulkan11Features device_11_features = {};
    device_11_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    device_11_features.pNext = &device_12_features;

    VkPhysicalDeviceFeatures2 device_features = {};
    device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    device_features.features.samplerAnisotropy = VK_TRUE;
    device_features.features.textureCompressionBC = VK_TRUE;
    device_features.pNext = &device_11_features;

#ifdef ENABLE_MEMORY_PRIORITIES
    VkPhysicalDevicePageableDeviceLocalMemoryFeaturesEXT paging_features = {};
    paging_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PAGEABLE_DEVICE_LOCAL_MEMORY_FEATURES_EXT;
    paging_features.pageableDeviceLocalMemory = VK_TRUE;
    VkPhysicalDeviceMemoryPriorityFeaturesEXT paging_prio_features = {};
    paging_prio_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PRIORITY_FEATURES_EXT;
    paging_prio_features.memoryPriority = VK_TRUE;
    paging_prio_features.pNext = &paging_features;
    paging_features.pNext = device_features.pNext;
    device_features.pNext = &paging_prio_features;
#endif

    VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features = {};
    VkPhysicalDeviceRayQueryFeaturesKHR rq_pipeline_features = {};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_pipeline_features = {};
    if (ref_data->ray_tracing_extension) {
        device_12_features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
        device_12_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

        as_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        as_features.accelerationStructure = VK_TRUE;
        as_features.pNext = device_features.pNext;
        device_features.pNext = &as_features;

        rq_pipeline_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rq_pipeline_features.rayQuery = VK_TRUE;
        rq_pipeline_features.pNext = device_features.pNext;
        device_features.pNext = &rq_pipeline_features;

        rt_pipeline_features.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rt_pipeline_features.rayTracingPipeline = VK_TRUE;
        rt_pipeline_features.pNext = device_features.pNext;
        device_features.pNext = &rt_pipeline_features;
    }

#ifdef ENABLE_FLOAT32_ATOMICS
    device_12_features.vulkanMemoryModel = VK_TRUE;
    device_12_features.vulkanMemoryModelDeviceScope = VK_TRUE;

    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomic_float_features = {};
    atomic_float_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT;
    atomic_float_features.shaderBufferFloat32Atomics = VK_TRUE;
    atomic_float_features.shaderImageFloat32Atomics = VK_TRUE;
#ifdef ENABLE_FLOAT32_ADD_ATOMICS
    atomic_float_features.shaderBufferFloat32AtomicAdd = VK_TRUE;
    atomic_float_features.shaderImageFloat32AtomicAdd = VK_TRUE;
#endif
    atomic_float_features.pNext = device_features.pNext;
    device_features.pNext = &atomic_float_features;
#endif

#ifdef ENABLE_SHADER_CLOCK // on-device profiling
    VkPhysicalDeviceShaderClockFeaturesKHR shader_clock_features = {};
    shader_clock_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
    shader_clock_features.shaderSubgroupClock = VK_TRUE;
    shader_clock_features.pNext = device_features.pNext;
    device_features.pNext = &shader_clock_features;
#endif

#ifdef ENABLE_REALTIME_RESOLVE
    // motion vectors stored in buffers half triplets for convenience
    device_11_features.storageBuffer16BitAccess = VK_TRUE;
    device_12_features.shaderFloat16 = VK_TRUE;
#endif

#ifdef ENABLE_CMM // cooperative matrices
    device_12_features.vulkanMemoryModel = VK_TRUE;

    VkPhysicalDeviceCooperativeMatrixFeaturesNV coop_matrix_features = {};
    coop_matrix_features.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_NV;
    coop_matrix_features.cooperativeMatrix = VK_TRUE;
    coop_matrix_features.pNext = device_features.pNext;
    device_features.pNext = &coop_matrix_features;
    println(CLL::INFORMATION, "Running with cooperative matrix support");

    VkPhysicalDeviceSubgroupSizeControlFeaturesEXT ssc_features = {};
    ssc_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_SIZE_CONTROL_FEATURES_EXT;
    ssc_features.subgroupSizeControl = VK_TRUE;
    ssc_features.computeFullSubgroups = VK_TRUE;
    ssc_features.pNext = device_features.pNext;
    device_features.pNext = &ssc_features;
#endif

#ifdef ENABLE_RASTER
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_features = {};
    dynamic_rendering_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    dynamic_rendering_features.dynamicRendering = VK_TRUE;
    dynamic_rendering_features.pNext = device_features.pNext;
    device_features.pNext = &dynamic_rendering_features;
#endif

    std::vector<const char *> device_extensions = vkrt::required_device_extensions;
    if (ref_data->ray_tracing_extension)
        device_extensions.insert(device_extensions.end(), ray_tracing_device_extensions.begin(), ray_tracing_device_extensions.end());
    for (const auto &ext : additional_extensions) {
        device_extensions.push_back(ext.c_str());
    }
    if (ref_data->non_semantic_info_extension)
        device_extensions.push_back(ref_data->non_semantic_info_extension);

    // Create the device
    VkDeviceCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.queueCreateInfoCount = secondaryQueue ? 2 : 1;
    create_info.pQueueCreateInfos = queue_create_info;
    create_info.enabledExtensionCount = device_extensions.size();
    create_info.ppEnabledExtensionNames = device_extensions.data();
    create_info.pEnabledFeatures = nullptr;
    create_info.pNext = &device_features;
    CHECK_VULKAN(vkCreateDevice(ref_data->vk_physical_device, &create_info, nullptr, &device));

    // Create the main and secondary queue
    vkGetDeviceQueue(device, ref_data->main_queue_index, 0, &ref_data->main_queue);
    if (secondaryQueue)
        vkGetDeviceQueue(device, ref_data->secondary_queue_index, 0, &ref_data->secondary_queue);
}

MemorySource::MemorySource(Device &device, int arena_idx, float memory_priority)
    : device(&device)
    , memory_priority(memory_priority) {
    if (arena_idx < 0)
        arena_idx += device.current_arena_index() + 1;
    this->arena_idx = arena_idx;
}

MemorySource::MemorySource(Device &device, MemorySourceArenas arena, float memory_priority)
    : device(&device)
    , memory_priority(memory_priority) {
    assert(arena == NewArenaSource);
    arena_idx = device.next_arena();
}

VkBufferCreateInfo Buffer::create_info(size_t nbytes, VkBufferUsageFlags usage)
{
    VkBufferCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = nbytes;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    return info;
}

// use make_buffer
Buffer::Buffer(init_tag) {
}

Buffer Buffer::make_buffer(MemorySource source,
                           size_t nbytes,
                           VkBufferUsageFlags usage,
                           ExtendedVkMemoryPropertyFlags mem_props,
                           int swap_buffer_count)
{
    auto& device = *source.device;
    auto vkdevice = device.logical_device();

    if (source.reuse) {
        if (source.reuse.ref_data->buf_size == nbytes
         && (int) source.reuse.ref_data->swap_count == swap_buffer_count
         && source.reuse.ref_data->usage == usage
         && source.reuse.ref_data->mem_props == mem_props)
            return source.reuse;
    }

    Buffer buf(init_tag{});
    buf.ref_data->swap_count = int_cast<uint16_t>(uint_bound(swap_buffer_count));
    buf.ref_data->buf_size = nbytes;
    buf.ref_data->usage = usage;
    buf.ref_data->mem_props = mem_props;
    buf.ref_data->vkdevice = device;
    buf.ref_data->host_visible = mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    auto create_info = Buffer::create_info(nbytes, usage);

    if (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        create_info.size = align_to(create_info.size, device.ref_data->hostBufferAtomSize);
    if (swap_buffer_count > 1) {
        create_info.size = align_to(create_info.size, device.ref_data->deviceBufferAlignment);
        if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
            create_info.size = align_to(create_info.size, 256);
    }

    buf.ref_data->swap_stride_padding = uint_bound(create_info.size - nbytes);

    if (swap_buffer_count > 1)
        create_info.size *= uint32_t(swap_buffer_count); // range checked above

#ifdef ENABLE_EXTERNAL_MEMORY
    VkExternalMemoryBufferCreateInfo exportInfo = {};
    if (mem_props & exported_memory_type_properties) {
        exportInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
        exportInfo.handleTypes = EXT_MEMORY_HANDLE;
        create_info.pNext = &exportInfo;
    }
#endif
    CHECK_VULKAN(vkCreateBuffer(vkdevice, &create_info, nullptr, &buf->buf));
    ++device.ref_data->mem_stats.total_buffers_created;

    try {
        VkMemoryRequirements mem_reqs = {};
        vkGetBufferMemoryRequirements(vkdevice, buf->buf, &mem_reqs);
        VkMemoryAllocateFlags allocFlags = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
                                            ? VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
                                            : 0;
        if (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            mem_reqs.alignment = std::max(mem_reqs.alignment, (VkDeviceSize) device.ref_data->hostBufferAlignment);
            mem_reqs.size = align_to(mem_reqs.size, device.ref_data->hostBufferAtomSize);
        }
        if (mem_props & EXVK_MEMORY_PROPERTY_SCRATCH_SPACE_ALIGNMENT) {
            // at least generic BVH scratch space has special alignment requirements
            mem_reqs.alignment = std::max(mem_reqs.alignment, (VkDeviceSize) device.ref_data->as_props.minAccelerationStructureScratchOffsetAlignment);
        }
        // note: this manual re-alignment is a bit surprising, but currently required at least on Mesa
        if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR)
            mem_reqs.alignment = std::max(mem_reqs.alignment, (VkDeviceSize) device.ref_data->as_props.minAccelerationStructureScratchOffsetAlignment);
        auto common_usage_bits = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        if (mem_props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
            common_usage_bits |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        else
            common_usage_bits |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        const bool is_common = source.arena_idx >= Device::ScratchArena && !(usage & ~common_usage_bits);
        const bool use_blocked_alloc = enable_blocked_alloc
            && source.arena_idx != Device::DisplayArena
            && !(mem_props & EXVK_MEMORY_PROPERTY_ZERO_BLOCK_OFFSET)
            ;
        auto allocation = device.alloc(source.arena_idx, mem_reqs.size
            , mem_reqs.memoryTypeBits, use_blocked_alloc ? mem_reqs.alignment : 0
            , VkMemoryPropertyFlags(mem_props & VkMemoryPropertyFlagsMask), allocFlags
            , is_common ? device.ref_data->commonAllocationBlockSize : 0
            , source.memory_priority);
        buf.ref_data->mem = allocation.memory;
        buf.ref_data->arena_idx = allocation.arena;
        buf.ref_data->type_idx = allocation.type;
        buf.ref_data->mem_offset = allocation.offset;
        buf.ref_data->mem_size = mem_reqs.size;
    } catch (...) {
        // not auto-released before mem is valid
        vkDestroyBuffer(vkdevice, buf->buf, nullptr);
        throw;
    }

    CHECK_VULKAN(vkBindBufferMemory(vkdevice, buf->buf, buf.ref_data->mem, buf.ref_data->mem_offset));

    return buf;
}

void Buffer::release_resources()
{
    if (ref_data->mem != VK_NULL_HANDLE) {
        if (ref_data->secondary)
            ref_data->secondary.release_resources();
        vkDestroyBuffer(ref_data->vkdevice->logical_device(), buf, nullptr);
        ref_data->vkdevice->free(ref_data->arena_idx, ref_data->type_idx, ref_data->mem, ref_data->mem_size);
    }
}

Buffer::~Buffer()
{
   this->discard_reference();
}

bool Buffer::enable_blocked_alloc = true;

Buffer Buffer::host(MemorySource source,
                    size_t nbytes,
                    VkBufferUsageFlags usage,
                    ExtendedVkMemoryPropertyFlags extra_mem_props,
                    int swap_buffer_count)
{
    ExtendedVkMemoryPropertyFlags memory_flags = extra_mem_props;
    memory_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
#ifndef CACHE_ALL_HOST_MEMORY
    // cache only memory likely to be read (e.g. readback)
    // note: DX upload heap is uncached, write-combined
    if (extra_mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
#endif
        memory_flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    return make_buffer(source, nbytes, usage
            , memory_flags
            , swap_buffer_count);
}

Buffer Buffer::device(MemorySource source,
                      size_t nbytes,
                      VkBufferUsageFlags usage,
                      ExtendedVkMemoryPropertyFlags extra_mem_props,
                      int swap_buffer_count)
{
    ExtendedVkMemoryPropertyFlags memory_flags = extra_mem_props;
#ifdef MINIMIZE_DEVICE_LOCAL_MEMORY
    if (source.memory_priority > 0.0f)
#endif
        memory_flags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (memory_flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        memory_flags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    return make_buffer(source, nbytes, usage
        , memory_flags
        , swap_buffer_count);
}

static const MemorySource NullMemorySourceInstance = nullptr;
const Buffer::MemorySource& Buffer::NullMemorySource = NullMemorySourceInstance;
Buffer Buffer::for_host(VkBufferUsageFlags usage, MemorySource const& source_, ExtendedVkMemoryPropertyFlags extra_mem_props) {
    MemorySource source = source_;
    if (!source.device)
        source = MemorySource(*ref_data->vkdevice, ref_data->arena_idx);
    return host(source, ref_data->buf_size, usage, extra_mem_props);
}

Buffer Buffer::secondary_for_host(VkBufferUsageFlags usage, ExtendedVkMemoryPropertyFlags extra_mem_props) {
    if (ref_data->secondary)
        return ref_data->secondary;
    return new_secondary_for_host(usage, extra_mem_props);
}
Buffer Buffer::new_secondary_for_host(VkBufferUsageFlags usage, ExtendedVkMemoryPropertyFlags extra_mem_props) {
    ref_data->secondary = for_host(usage, nullptr, extra_mem_props);
    return ref_data->secondary;
}

void *Buffer::map()
{
    VkDeviceSize offset = 0;
    VkDeviceSize size = ref_data->mem_size;
    if (ref_data->swap_count > 1) {
        offset = swap_offset();
        size = ref_data->buf_size + ref_data->swap_stride_padding;
    }
    else
        ref_data->fully_mapped_and_undefined = !(ref_data->mem_props & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    return map(offset, size);
}

void *Buffer::map(size_t offset, size_t size)
{
    assert(ref_data->host_visible);
    assert(offset + size <= ref_data->mem_size);
    void *mapping = nullptr;
    CHECK_VULKAN(vkMapMemory(ref_data->vkdevice->logical_device(),
                 ref_data->mem,
                 ref_data->mem_offset + offset,
                 size,
                 0,
                 &mapping));
    return mapping;
}

void Buffer::invalidate_all()
{
    assert(ref_data->host_visible);
    VkMappedMemoryRange range = { };
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = ref_data->mem;
    range.offset = ref_data->mem_offset + swap_offset();
    range.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(ref_data->vkdevice->logical_device(), 1, &range);
    ref_data->fully_mapped_and_undefined = false;
}

void Buffer::flush_all()
{
    assert(ref_data->host_visible);
    VkMappedMemoryRange range = { };
    range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.memory = ref_data->mem;
    range.offset = ref_data->mem_offset + swap_offset();
    range.size = VK_WHOLE_SIZE;
    vkFlushMappedMemoryRanges(ref_data->vkdevice->logical_device(), 1, &range);
    ref_data->fully_mapped_and_undefined = false;
}

void Buffer::unmap()
{
    assert(ref_data->host_visible);
    if (ref_data->fully_mapped_and_undefined)
        flush_all();
    vkUnmapMemory(ref_data->vkdevice->logical_device(), ref_data->mem);
}

size_t Buffer::size() const
{
    return ref_data->buf_size;
}

size_t Buffer::swap_offset() const
{
    size_t buf_stride = ref_data->buf_size + ref_data->swap_stride_padding;
    return buf_stride * ref_data->swap_idx;
}

void Buffer::cycle_swap(int swap_count) {
    assert(swap_count >= 1);
    assert(swap_count <= int(ref_data->swap_count));
    ref_data->swap_idx = uint16_t(int(ref_data->swap_idx + 1) % swap_count);
}

VkDeviceAddress Buffer::device_address() const
{
    VkBufferDeviceAddressInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    buf_info.buffer = buf;
    return vkGetBufferDeviceAddress(ref_data->vkdevice->logical_device(), &buf_info);
}

Buffer& Buffer::secondary() const
{
    return ref_data->secondary;
}

static const uint16_t AliasMemoryType = uint16_t(~0u);

void Texture2D::release_resources()
{
    if (ref_data->mem != VK_NULL_HANDLE)
    {
        // Destroy the main view
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(ref_data->vkdevice->logical_device(), view, nullptr);

        // Destroy the mip views (if any)
        for (int mipVIdx = 0; mipVIdx < ref_data->mipViews.size(); ++mipVIdx)
            vkDestroyImageView(ref_data->vkdevice->logical_device(), ref_data->mipViews[mipVIdx], nullptr);

        vkDestroyImage(ref_data->vkdevice->logical_device(), image, nullptr);
        if (ref_data->type_idx != AliasMemoryType)
            ref_data->vkdevice->free(ref_data->arena_idx, ref_data->type_idx, ref_data->mem, ref_data->mem_size);
    }
}

Texture2D::~Texture2D()
{
    this->discard_reference();
}

Texture2D::Texture2D(init_tag) {
}

Texture2D Texture2D::device(MemorySource source,
                            glm::ivec4 dims,
                            VkFormat img_format,
                            VkImageUsageFlags usage,
                            VkImageTiling tiling)
{
    auto& device = *source.device;
    auto vkdevice = device.logical_device();

    if (source.reuse) {
        if (source.reuse.tdims == glm::ivec2(dims)
         && source.reuse.mips == (dims.w > 1 ? dims.w : 1)
         && source.reuse.layers == (dims.z > 1 ? dims.z : 1)
         && source.reuse.ref_data->img_format == img_format
         && source.reuse.ref_data->usage == usage)
            return source.reuse;
    }

    Texture2D texture(init_tag{});
    texture.ref_data->img_format = img_format;
    texture.ref_data->usage = usage;
    texture->tdims = glm::ivec2(dims);
    texture->layers = dims.z > 1 ? dims.z : 1;
    texture->mips = dims.w > 1 ? dims.w : 1;
    texture.ref_data->vkdevice = device;

    VkImageCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_2D;
    create_info.format = img_format;
    create_info.extent.width = uint_bound(texture->tdims.x);
    create_info.extent.height = uint_bound(texture->tdims.y);
    create_info.extent.depth = 1;
    create_info.mipLevels = texture->mips;
    create_info.arrayLayers = texture->layers;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = tiling;
    create_info.usage = usage;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#ifdef ENABLE_EXTERNAL_MEMORY
    VkExternalMemoryImageCreateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    exportInfo.handleTypes = EXT_MEMORY_HANDLE;
    create_info.pNext = &exportInfo;
#endif
    CHECK_VULKAN(
        vkCreateImage(vkdevice, &create_info, nullptr, &texture->image));
    ++device.ref_data->mem_stats.total_images_created;

    try {
        VkMemoryRequirements mem_reqs = {};
        vkGetImageMemoryRequirements(vkdevice, texture->image, &mem_reqs);
        const bool is_target = (usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
                               (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
                               (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
                               (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) ||
                               (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) ||
                               (usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR);
        const bool is_common = source.arena_idx >= Device::DefaultArenaCount && !is_target;
        Device::Allocation allocation;
        if (source.alias) {
            allocation.arena = source.alias.ref_data->arena_idx;
            allocation.type = source.alias.ref_data->type_idx;
            allocation.memory = source.alias.ref_data->mem;
            allocation.offset = source.alias.ref_data->mem_offset;

            if (allocation.offset % mem_reqs.alignment != 0)
                throw_error("Aliased storage space is misaligned");
            if (source.alias.ref_data->mem_size < mem_reqs.size)
                throw_error("Aliased storage space is insufficient");

            allocation.type = AliasMemoryType;
        }
        else
            allocation = device.alloc(source.arena_idx, mem_reqs.size
                , mem_reqs.memoryTypeBits, is_target ? 0 : mem_reqs.alignment
                , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                , 0
                , is_common ? device.ref_data->commonAllocationBlockSize : 0);
        texture.ref_data->mem = allocation.memory;
        texture.ref_data->mem_offset = allocation.offset;
        texture.ref_data->mem_size = mem_reqs.size;
        texture.ref_data->arena_idx = allocation.arena;
        texture.ref_data->type_idx = allocation.type;
    } catch (...) {
        // not auto-released before mem is valid
        vkDestroyImage(vkdevice, texture->image, nullptr);
        throw;
    }

    CHECK_VULKAN(vkBindImageMemory(vkdevice, texture->image, texture.ref_data->mem, texture.ref_data->mem_offset));

    // An ImageView is only valid for certain image types, so check that the image being made
    // is one of those
    const bool make_view = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) ||
                           (usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
                           (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
                           (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
                           (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    if (make_view) {
        VkImageViewCreateInfo view_create_info = {};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = texture->image;
        view_create_info.viewType = texture->layers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view_create_info.format = create_info.format;

        view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        else
            view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = create_info.mipLevels;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = create_info.arrayLayers;

        CHECK_VULKAN(vkCreateImageView(
            vkdevice, &view_create_info, nullptr, &texture->view));
    }
    return texture;
}

void Texture2D::allocate_mip_views()
{
    assert(ref_data->mipViews.size() == 0);

    ref_data->mipViews.resize(mips);
    for (int mipIdx = 0; mipIdx < mips; ++mipIdx)
    {
        VkImageViewCreateInfo view_create_info = {};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = image;
        view_create_info.viewType = layers == 1 ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view_create_info.format = ref_data->img_format;

        view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = mipIdx;
        view_create_info.subresourceRange.levelCount = 1;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = layers;
        CHECK_VULKAN(vkCreateImageView(ref_data->vkdevice->logical_device(), &view_create_info, nullptr, &ref_data->mipViews[mipIdx]));
    }
}

size_t Texture2D::pixel_size() const
{
    switch (ref_data->img_format) {
    case VK_FORMAT_R16_UINT:
        return 2;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        return 4;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        throw std::runtime_error("Unhandled image format!");
    }
}

VkFormat Texture2D::pixel_format() const
{
    return ref_data->img_format;
}

glm::ivec2 Texture2D::dims() const
{
    return tdims;
}

void Texture3D::release_resources()
{
    if (ref_data->mem != VK_NULL_HANDLE) {
        if (view != VK_NULL_HANDLE)
            vkDestroyImageView(ref_data->vkdevice->logical_device(), view, nullptr);
        vkDestroyImage(ref_data->vkdevice->logical_device(), image, nullptr);
        if (ref_data->type_idx != AliasMemoryType)
            ref_data->vkdevice->free(
                ref_data->arena_idx, ref_data->type_idx, ref_data->mem, ref_data->mem_size);
    }
}

Texture3D::~Texture3D()
{
    this->discard_reference();
}

Texture3D::Texture3D(init_tag) {}

Texture3D Texture3D::device(MemorySource source,
                            glm::ivec4 dims,
                            VkFormat img_format,
                            VkImageUsageFlags usage)
{
    auto &device = *source.device;
    auto vkdevice = device.logical_device();

    if (source.reuse) {
        if (source.reuse.tdims == glm::ivec3(dims) &&
            source.reuse.mips == (dims.w > 1 ? dims.w : 1) &&
            source.reuse.layers == 1 &&
            source.reuse.ref_data->img_format == img_format &&
            source.reuse.ref_data->usage == usage)
            return source.reuse;
    }

    Texture3D texture(init_tag{});
    texture.ref_data->img_format = img_format;
    texture.ref_data->usage = usage;
    texture->tdims = glm::ivec3(dims);
    texture->layers = 1;
    texture->mips = dims.w > 1 ? dims.w : 1;
    texture.ref_data->vkdevice = device;

    VkImageCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.imageType = VK_IMAGE_TYPE_3D;
    create_info.format = img_format;
    create_info.extent.width = uint_bound(texture->tdims.x);
    create_info.extent.height = uint_bound(texture->tdims.y);
    create_info.extent.depth = uint_bound(texture->tdims.z);
    create_info.mipLevels = texture->mips;
    create_info.arrayLayers = texture->layers;
    create_info.samples = VK_SAMPLE_COUNT_1_BIT;
    create_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    create_info.usage = usage;
    create_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
#ifdef ENABLE_EXTERNAL_MEMORY
    VkExternalMemoryImageCreateInfo exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    exportInfo.handleTypes = EXT_MEMORY_HANDLE;
    create_info.pNext = &exportInfo;
#endif
    CHECK_VULKAN(vkCreateImage(vkdevice, &create_info, nullptr, &texture->image));
    ++device.ref_data->mem_stats.total_images_created;

    try {
        VkMemoryRequirements mem_reqs = {};
        vkGetImageMemoryRequirements(vkdevice, texture->image, &mem_reqs);
        const bool is_target =
            (usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
            (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
            (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
            (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) ||
            (usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) ||
            (usage & VK_IMAGE_USAGE_FRAGMENT_SHADING_RATE_ATTACHMENT_BIT_KHR);
        const bool is_common = source.arena_idx >= Device::DefaultArenaCount && !is_target;
        Device::Allocation allocation;
        if (source.alias) {
            allocation.arena = source.alias.ref_data->arena_idx;
            allocation.type = source.alias.ref_data->type_idx;
            allocation.memory = source.alias.ref_data->mem;
            allocation.offset = source.alias.ref_data->mem_offset;

            if (allocation.offset % mem_reqs.alignment != 0)
                throw_error("Aliased storage space is misaligned");
            if (source.alias.ref_data->mem_size < mem_reqs.size)
                throw_error("Aliased storage space is insufficient");

            allocation.type = AliasMemoryType;
        } else
            allocation =
                device.alloc(source.arena_idx,
                             mem_reqs.size,
                             mem_reqs.memoryTypeBits,
                             is_target ? 0 : mem_reqs.alignment,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             0,
                             is_common ? device.ref_data->commonAllocationBlockSize : 0);
        texture.ref_data->mem = allocation.memory;
        texture.ref_data->mem_offset = allocation.offset;
        texture.ref_data->mem_size = mem_reqs.size;
        texture.ref_data->arena_idx = allocation.arena;
        texture.ref_data->type_idx = allocation.type;
    } catch (...) {
        // not auto-released before mem is valid
        vkDestroyImage(vkdevice, texture->image, nullptr);
        throw;
    }

    CHECK_VULKAN(vkBindImageMemory(
        vkdevice, texture->image, texture.ref_data->mem, texture.ref_data->mem_offset));

    // An ImageView is only valid for certain image types, so check that the image being made
    // is one of those
    const bool make_view = (usage & VK_IMAGE_USAGE_SAMPLED_BIT) ||
                           (usage & VK_IMAGE_USAGE_STORAGE_BIT) ||
                           (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
                           (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ||
                           (usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
    if (make_view) {
        VkImageViewCreateInfo view_create_info = {};
        view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_create_info.image = texture->image;
        view_create_info.viewType = VK_IMAGE_VIEW_TYPE_3D;
        view_create_info.format = create_info.format;

        view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
            view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        else
            view_create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_create_info.subresourceRange.baseMipLevel = 0;
        view_create_info.subresourceRange.levelCount = create_info.mipLevels;
        view_create_info.subresourceRange.baseArrayLayer = 0;
        view_create_info.subresourceRange.layerCount = create_info.arrayLayers;

        CHECK_VULKAN(vkCreateImageView(vkdevice, &view_create_info, nullptr, &texture->view));
    }
    return texture;
}

size_t Texture3D::pixel_size() const
{
    switch (ref_data->img_format) {
    case VK_FORMAT_R16_UINT:
        return 2;
    case VK_FORMAT_R32_UINT:
    case VK_FORMAT_R32_SFLOAT:
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
        return 4;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        throw std::runtime_error("Unhandled image format!");
    }
}

VkFormat Texture3D::pixel_format() const
{
    return ref_data->img_format;
}

glm::ivec3 Texture3D::dims() const
{
    return tdims;
}

ShaderModule::ShaderModule(Device &device, std::vector<char> const& code)
    : ShaderModule(device, (uint32_t const*) code.data(), code.size()) {
}

ShaderModule::ShaderModule(Device &vkdevice, const uint32_t *code, size_t code_size)
{
    ref_data->vkdevice = vkdevice.logical_device();

    VkShaderModuleCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code_size;
    info.pCode = code;

    VkShaderModuleValidationCacheCreateInfoEXT cache_info = {};
    if (auto cache = vkdevice.validation_cache()) {
        cache_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_VALIDATION_CACHE_CREATE_INFO_EXT;
        cache_info.validationCache = cache;
        info.pNext = &cache_info;
    }

    CHECK_VULKAN(vkCreateShaderModule(vkdevice.logical_device(), &info, nullptr, &module));
}

void ShaderModule::release_resources()
{
    if (module != VK_NULL_HANDLE && ref_data->vkdevice != VK_NULL_HANDLE) {
        vkDestroyShaderModule(ref_data->vkdevice, module, nullptr);
        ref_data->vkdevice = VK_NULL_HANDLE;
    }
}

ShaderModule::~ShaderModule()
{
    this->discard_reference();
}

DescriptorSetLayoutBuilder &DescriptorSetLayoutBuilder::add_binding(uint32_t binding,
                                                                    uint32_t count,
                                                                    VkDescriptorType type,
                                                                    uint32_t stage_flags,
                                                                    uint32_t ext_flags)
{
    VkDescriptorSetLayoutBinding desc = {};
    desc.binding = binding;
    desc.descriptorCount = count;
    desc.descriptorType = type;
    desc.stageFlags = stage_flags;
    bindings.push_back(desc);
    binding_ext_flags.push_back(ext_flags | default_ext_flags);
    return *this;
}

VkDescriptorSetLayout DescriptorSetLayoutBuilder::build(Device &device) const
{
    VkDescriptorSetLayoutBindingFlagsCreateInfo ext_flags = {};
    ext_flags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    ext_flags.bindingCount = binding_ext_flags.size();
    ext_flags.pBindingFlags = binding_ext_flags.data();

    VkDescriptorSetLayoutCreateInfo create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.bindingCount = bindings.size();
    create_info.pBindings = bindings.data();
    create_info.pNext = &ext_flags;

    // note: we assume all binding flags include this or none
    if (!binding_ext_flags.empty() && binding_ext_flags[0] & VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT)
        create_info.flags |= VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    CHECK_VULKAN(
        vkCreateDescriptorSetLayout(device.logical_device(), &create_info, nullptr, &layout));
    return layout;
}

VkDescriptorPool DescriptorSetLayoutBuilder::build_compatible_pool(Device &device, int multiplicity) const {
    std::vector<VkDescriptorPoolSize> pool_sizes;
    std::transform(
        bindings.begin(),
        bindings.end(),
        std::back_inserter(pool_sizes),
        [&](const VkDescriptorSetLayoutBinding &b) {
            VkDescriptorPoolSize ps = {};
            ps.type = b.descriptorType;
            ps.descriptorCount = b.descriptorCount * multiplicity;
            return ps;
        });

    VkDescriptorPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_create_info.maxSets = multiplicity;
    pool_create_info.poolSizeCount = pool_sizes.size();
    pool_create_info.pPoolSizes = pool_sizes.data();

    VkDescriptorPool desc_pool = VK_NULL_HANDLE;
    CHECK_VULKAN(vkCreateDescriptorPool(
        device->logical_device(), &pool_create_info, nullptr, &desc_pool));
    return desc_pool;
}

DescriptorSetUpdater &DescriptorSetUpdater::write_acceleration_structures(
    VkDescriptorSet set, uint32_t binding,
    VkAccelerationStructureKHR const* bvh, int count)
{
    VkWriteDescriptorSetAccelerationStructureKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    info.accelerationStructureCount = uint_bound(count);
    info.pAccelerationStructures = bvh;

    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = info.accelerationStructureCount;
    write.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    write.as_index = accel_structs.size();

    accel_structs.push_back(info);
    writes.push_back(write);
    return *this;
}

DescriptorSetUpdater &DescriptorSetUpdater::write_storage_image(
    VkDescriptorSet set, uint32_t binding, Texture2D const &img)
{
    auto imageView = img->view_handle();
    return write_storage_image_views(set, binding, &imageView, 1);
}

DescriptorSetUpdater &DescriptorSetUpdater::write_storage_image_mip(VkDescriptorSet set,
    uint32_t binding,
    const Texture2D& img,
    uint32_t mip)
{
    auto imageView = img->view_handle_mip(mip);
    return write_storage_image_views(set, binding, &imageView, 1);
}

DescriptorSetUpdater &DescriptorSetUpdater::write_storage_image(VkDescriptorSet set,
                                                                uint32_t binding,
                                                                Texture3D const &img)
{
    auto imageView = img->view_handle();
    return write_storage_image_views(set, binding, &imageView, 1);
}

DescriptorSetUpdater &DescriptorSetUpdater::write_storage_image_views(VkDescriptorSet set,
                                                                      uint32_t binding,
                                                                      const VkImageView *imgs, int count)
{
    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = uint_bound(count);
    write.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.img_index = images.size();

    VkDescriptorImageInfo img_desc = {};
    img_desc.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    images.reserve(images.size() + write.count);
    for (int i = 0; i < count; ++i) {
        img_desc.imageView = imgs[i];
        images.push_back(img_desc);
    }

    writes.push_back(write);
    return *this;
}

DescriptorSetUpdater &DescriptorSetUpdater::write_ubo(VkDescriptorSet set,
                                                      uint32_t binding,
                                                      const Buffer &buf)
{
    VkDescriptorBufferInfo buf_desc = {};
    buf_desc.buffer = buf->handle();
    buf_desc.offset = buf->swap_offset();
    buf_desc.range = buf->size();

    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = 1;
    write.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.buf_index = buffers.size();

    buffers.push_back(buf_desc);
    writes.push_back(write);
    return *this;
}

DescriptorSetUpdater &DescriptorSetUpdater::write_ssbo(VkDescriptorSet set,
                                                       uint32_t binding,
                                                       const Buffer &buf)
{
    return write_ssbo_array(set, binding, &buf, 1);
}

DescriptorSetUpdater &DescriptorSetUpdater::write_ssbo_array(
    VkDescriptorSet set, uint32_t binding, const Buffer *bufs, int count)
{
    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = uint_bound(count);
    write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buf_index = buffers.size();

    VkDescriptorBufferInfo buf_desc = {};
    buffers.reserve(buffers.size() + write.count);
    for (int i = 0; i < count; ++i) {
        auto& b = bufs[i];
        buf_desc.buffer = b->handle();
        buf_desc.offset = b->swap_offset();
        buf_desc.range = b->size();
        buffers.push_back(buf_desc);
    }

    writes.push_back(write);
    return *this;
}

DescriptorSetUpdater &DescriptorSetUpdater::write_combined_sampler_array(
    VkDescriptorSet set,
    uint32_t binding,
    const std::vector<Texture2D> &textures,
    const std::vector<VkSampler> &samplers)
{
    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = textures.size();
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.img_index = images.size();
    images.resize(write.img_index + write.count);

    size_t sampler_count = samplers.size();
    for (size_t i = 0; i < write.count; ++i) {
        VkDescriptorImageInfo desc = {};
        desc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        desc.imageView = textures[i]->view_handle();
        assert(desc.imageView);
        desc.sampler = samplers[sampler_count == 1 ? 0 : i];
        assert(desc.sampler);
        images[write.img_index + i] = desc;
    }

    writes.push_back(write);
    return *this;
}

    DescriptorSetUpdater &DescriptorSetUpdater::write_combined_sampler(VkDescriptorSet set,
                                             uint32_t binding,
                                             const Texture2D &textures,
                                             VkSampler samplers)
{
    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = 1;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.img_index = images.size();
    images.resize(write.img_index + write.count);

    VkDescriptorImageInfo desc = {};
    desc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    desc.imageView = textures->view_handle();
    assert(desc.imageView);
    desc.sampler = samplers;
    assert(desc.sampler);
    images[write.img_index] = desc;

    writes.push_back(write);
    return *this;
}

DescriptorSetUpdater &DescriptorSetUpdater::write_combined_sampler(VkDescriptorSet set,
                                                                   uint32_t binding,
                                                                   const Texture3D &textures,
                                                                   VkSampler samplers)
{
    WriteDescriptorInfo write;
    write.dst_set = set;
    write.binding = binding;
    write.count = 1;
    write.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.img_index = images.size();
    images.resize(write.img_index + write.count);

    VkDescriptorImageInfo desc = {};
    desc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    desc.imageView = textures->view_handle();
    assert(desc.imageView);
    desc.sampler = samplers;
    assert(desc.sampler);
    images[write.img_index] = desc;

    writes.push_back(write);
    return *this;
}

void DescriptorSetUpdater::update(Device &device)
{
    std::vector<VkWriteDescriptorSet> desc_writes;
    std::transform(
        writes.begin(),
        writes.end(),
        std::back_inserter(desc_writes),
        [&](const WriteDescriptorInfo &w) {
            VkWriteDescriptorSet wd = {};
            wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            wd.dstSet = w.dst_set;
            wd.dstBinding = w.binding;
            wd.descriptorCount = w.count;
            wd.descriptorType = w.type;

            if (wd.descriptorType == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR) {
                wd.pNext = &accel_structs[w.as_index];
            } else if (wd.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                       wd.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                wd.pBufferInfo = &buffers[w.buf_index];
            } else if (wd.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                       wd.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                wd.pImageInfo = &images[w.img_index];
            }
            return wd;
        });
    vkUpdateDescriptorSets(
        device.logical_device(), desc_writes.size(), desc_writes.data(), 0, nullptr);
}

void DescriptorSetUpdater::reset() {
    *this = DescriptorSetUpdater();
}

VkResult build_compute_pipeline(Device &device, VkPipeline* pipeline, VkPipelineLayout layout, const ShaderModule &shader, char const * entry_point)
{
    VkPipelineShaderStageCreateInfo ss_ci = {};
    ss_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    ss_ci.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    ss_ci.module = shader.module;
    ss_ci.pName = entry_point;

    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo rsgs_ci = {};
    if (device.ref_data->subgroupSize) {
        rsgs_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO;
        rsgs_ci.requiredSubgroupSize = device.ref_data->subgroupSize;
        rsgs_ci.pNext = (void*) ss_ci.pNext;
        ss_ci.pNext = &rsgs_ci;
        ss_ci.flags |= VK_PIPELINE_SHADER_STAGE_CREATE_REQUIRE_FULL_SUBGROUPS_BIT_EXT;
    }

    VkComputePipelineCreateInfo pipeline_create_info = {};
    pipeline_create_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_create_info.stage = ss_ci;
    pipeline_create_info.layout = layout;
    auto result = vkCreateComputePipelines(device.logical_device(),
                                               device.pipeline_cache(),
                                               1,
                                               &pipeline_create_info,
                                               nullptr,
                                               pipeline);

    return result;
}

void get_workgroup_size(char const* const* defines, int* x, int* y, int* z) {
    for (int i = 0; defines[i]; ++i) {
        if (x && 1 == sscanf(defines[i], "WORKGROUP_SIZE_X=%d", x))
            x = nullptr;
        if (y && 1 == sscanf(defines[i], "WORKGROUP_SIZE_Y=%d", y))
            y = nullptr;
        if (z && 1 == sscanf(defines[i], "WORKGROUP_SIZE_Z=%d", z))
            z = nullptr;
    }

    // Workgroup size z is optional, if not specified, set the value to 1
    if (z)
    {
        *z = 1;
        z = nullptr;
    }

    if (x || y || z)
        throw_error("Missing workgroup size(s)");
}

void CommandStream::hold_buffer(Buffer const& buf) {
    throw_error("Buffer holding not implemented for this kind of command stream");
}
void CommandStream::hold_texture(Texture2D const& buf) {
    throw_error("Buffer holding not implemented for this kind of command stream");
}

SyncCommandStream::SyncCommandStream(Device &device, CommandQueueType type)
{
    vkdevice = device.logical_device();

    command_pool = device->make_command_pool(type, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = command_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = 1;
        CHECK_VULKAN(
            vkAllocateCommandBuffers(device->logical_device(), &info, &current_buffer));
    }

    {
        VkFenceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        CHECK_VULKAN(vkCreateFence(device->logical_device(), &info, nullptr, &current_fence));
    }

    current_queue = type == CommandQueueType::Main ? device.main_queue() : device.secondary_queue();
}
SyncCommandStream::~SyncCommandStream() {
    this->discard_reference();
}
void SyncCommandStream::release_resources() {
    if (vkdevice) {
        vkDestroyCommandPool(vkdevice, command_pool, nullptr);
        vkDestroyFence(vkdevice, current_fence, nullptr);
        vkdevice = VK_NULL_HANDLE;
    }
}

void SyncCommandStream::begin_record() {
    this->SyncCommandStream::wait_complete();
    CHECK_VULKAN(vkResetFences(vkdevice, 1, &current_fence));
    
    CHECK_VULKAN(vkResetCommandPool(vkdevice, command_pool, 0));

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(current_buffer, &begin_info));
}

void SyncCommandStream::end_submit(bool only_manual_wait) {
    CHECK_VULKAN(vkEndCommandBuffer(current_buffer));
        
    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &current_buffer;
    CHECK_VULKAN(
        vkQueueSubmit(current_queue, 1, &submit_info, current_fence));

    if (!only_manual_wait)
        this->SyncCommandStream::wait_complete();
}

void SyncCommandStream::end_submit(const SubmitParameters* submit_params)
{
    CHECK_VULKAN(vkEndCommandBuffer(current_buffer));

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &current_buffer;

    // Wait semaphores
    if (submit_params->num_wait_semaphores != 0)
    {
        submit_info.waitSemaphoreCount = submit_params->num_wait_semaphores;
        submit_info.pWaitSemaphores = submit_params->wait_semaphore_array.data();
        submit_info.pWaitDstStageMask = submit_params->wait_flag_array.data();
    }

    // Signal semaphores
    if (submit_params->num_signal_semaphore != 0)
    {
        submit_info.signalSemaphoreCount = submit_params->num_signal_semaphore;
        submit_info.pSignalSemaphores = submit_params->signal_semaphore_array.data();
    }
    CHECK_VULKAN(vkQueueSubmit(current_queue, 1, &submit_info, current_fence));
}

void SyncCommandStream::wait_complete(int cursor) {
    CHECK_VULKAN(vkWaitForFences(vkdevice, 1, &current_fence, VK_TRUE, std::numeric_limits<uint64_t>::max()));
}

void SyncCommandStream::release_command_buffers() {
    CHECK_VULKAN(vkResetCommandPool(vkdevice, command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));
}

void SyncCommandStream::hold_buffer(Buffer const& buf) {
    // not required for synchronous stream
}
void SyncCommandStream::hold_texture(Texture2D const& buf) {
    // not required for synchronous stream
}

AsyncCommandStream::AsyncCommandStream(Device& device, CommandQueueType type, int async_command_buffer_count) {
    ref_data->vkdevice = device.logical_device();
    ref_data->async_command_buffer_count = async_command_buffer_count;

    ref_data->command_pool = device->make_command_pool(type, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = ref_data->command_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = async_command_buffer_count;
        CHECK_VULKAN(
            vkAllocateCommandBuffers(device->logical_device(), &info, ref_data->async_command_buffers));
    }
    {
        VkSemaphoreTypeCreateInfo timeline_create_info = {};
        timeline_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
        timeline_create_info.pNext = NULL;
        timeline_create_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_create_info.initialValue = 0;
        VkSemaphoreCreateInfo semaphore_info = {};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphore_info.pNext = &timeline_create_info;
        semaphore_info.flags = 0;
        CHECK_VULKAN(
            vkCreateSemaphore(device->logical_device(), &semaphore_info, NULL, &ref_data->async_command_timeline));
    }

    current_queue = type == CommandQueueType::Main ? device.main_queue() : device.secondary_queue();
}
AsyncCommandStream::~AsyncCommandStream() {
    this->discard_reference();
}
void AsyncCommandStream::release_resources() {
    if (ref_data->vkdevice) {
        vkDestroyCommandPool(ref_data->vkdevice, ref_data->command_pool, nullptr);
        vkDestroySemaphore(ref_data->vkdevice, ref_data->async_command_timeline, nullptr);
    }
}

void AsyncCommandStream::release_command_buffers() {
    CHECK_VULKAN(vkResetCommandPool(ref_data->vkdevice, ref_data->command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));
    for (int i = 0; i < ref_data->async_command_buffer_count; ++i)
        ref_data->async_resources[i] = ResourceStore();
}

void AsyncCommandStream::begin_record() {
    int next_idx = ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count;
    auto next_command_buffer = ref_data->async_command_buffers[next_idx];

    int finished_cursor = ref_data->async_command_buffer_cursor - ref_data->async_command_buffer_count;
    // check if already pending
    if (finished_cursor >= 0) {
        this->AsyncCommandStream::wait_complete(finished_cursor);
        CHECK_VULKAN(vkResetCommandBuffer(next_command_buffer, 0));
    }

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(next_command_buffer, &begin_info));
    
    current_buffer = next_command_buffer;
}

void AsyncCommandStream::end_submit(bool only_manual_wait) {
    auto next_command_buffer = ref_data->async_command_buffers[ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count];
    assert(current_buffer == next_command_buffer);
    CHECK_VULKAN(vkEndCommandBuffer(next_command_buffer));

    const uint64_t waitValue = ref_data->async_command_buffer_cursor;
    const uint64_t signalValue = waitValue + 1;

    VkTimelineSemaphoreSubmitInfo timeline_info = { };
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.pNext = NULL;
    if (ref_data->async_command_buffer_cursor > 0) {
        timeline_info.waitSemaphoreValueCount = 1;
        timeline_info.pWaitSemaphoreValues = &waitValue;
    }
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues = &signalValue;

    VkSubmitInfo submit_info = { };
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &next_command_buffer;
    submit_info.pNext = &timeline_info;
    if (ref_data->async_command_buffer_cursor > 0) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &ref_data->async_command_timeline;
    }
    submit_info.signalSemaphoreCount  = 1;
    submit_info.pSignalSemaphores = &ref_data->async_command_timeline;
    VkFlags waitDstStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    submit_info.pWaitDstStageMask = &waitDstStageFlags;
    CHECK_VULKAN(
        vkQueueSubmit(current_queue, 1, &submit_info, VK_NULL_HANDLE));

    // command buffer pending
    ++ref_data->async_command_buffer_cursor;
}

void AsyncCommandStream::end_submit(const SubmitParameters *submit_params) {
    // For now we're not implementing this one.
    return;
}

void AsyncCommandStream::wait_complete(int cursor) {
    if (cursor < 0) {
        cursor += ref_data->async_command_buffer_cursor;
        if (cursor < 0) return; // was not pending so far
    }
    const uint64_t waitValue = cursor+1;

    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &ref_data->async_command_timeline;
    waitInfo.pValues = &waitValue;

    CHECK_VULKAN(
        vkWaitSemaphores(ref_data->vkdevice, &waitInfo, UINT64_MAX));

    int cmd_buf_count = ref_data->async_command_buffer_count;
    int cursor_in_flight = ref_data->async_command_buffer_cursor - cmd_buf_count;
    for (; cursor_in_flight <= cursor; ++cursor_in_flight) {
        if (cursor_in_flight < 0) continue;
        ref_data->async_resources[cursor_in_flight % cmd_buf_count] = ResourceStore();
    }
}

void AsyncCommandStream::hold_buffer(Buffer const& buf) {
    int current_idx = ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count;
    assert(current_buffer == ref_data->async_command_buffers[current_idx]);
    ref_data->async_resources[current_idx].buffers.push_back(buf);
}

void AsyncCommandStream::hold_texture(Texture2D const& tex) {
    int current_idx = ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count;
    assert(current_buffer == ref_data->async_command_buffers[current_idx]);
    ref_data->async_resources[current_idx].textures.push_back(tex);
}

ParallelCommandStream::ParallelCommandStream(Device& device, CommandQueueType type, int async_command_buffer_count) {
    ref_data->vkdevice = device.logical_device();
    ref_data->async_command_buffer_count = async_command_buffer_count;

    ref_data->command_pool = device->make_command_pool(type, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    {
        VkCommandBufferAllocateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool = ref_data->command_pool;
        info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = async_command_buffer_count;
        CHECK_VULKAN(
            vkAllocateCommandBuffers(device->logical_device(), &info, ref_data->async_command_buffers));
    }
    {
        VkFenceCreateInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < async_command_buffer_count; ++i)
            CHECK_VULKAN(vkCreateFence(device->logical_device(), &info, nullptr, &ref_data->async_fences[i]));
    }

    current_queue = type == CommandQueueType::Main ? device.main_queue() : device.secondary_queue();
}

ParallelCommandStream::~ParallelCommandStream() {
    this->discard_reference();
}
void ParallelCommandStream::release_resources() {
    if (ref_data->vkdevice) {
        vkDestroyCommandPool(ref_data->vkdevice, ref_data->command_pool, nullptr);
        for (int i = 0; i < MAX_ASYNC_COMMAND_BUFFERS; ++i)
            vkDestroyFence(ref_data->vkdevice, ref_data->async_fences[i], nullptr);
    }
}

void ParallelCommandStream::release_command_buffers() {
    CHECK_VULKAN(vkResetCommandPool(ref_data->vkdevice, ref_data->command_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));
}

void ParallelCommandStream::begin_record() {
    int swap_idx = int(ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count);
    auto next_command_buffer = ref_data->async_command_buffers[swap_idx];

    this->ParallelCommandStream::wait_complete(-ref_data->async_command_buffer_count);
    CHECK_VULKAN(vkResetFences(ref_data->vkdevice, 1, &ref_data->async_fences[swap_idx]));

    CHECK_VULKAN(vkResetCommandBuffer(next_command_buffer, 0));

    VkCommandBufferBeginInfo begin_info = {};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CHECK_VULKAN(vkBeginCommandBuffer(next_command_buffer, &begin_info));
    
    current_buffer = next_command_buffer;
    current_fence = ref_data->async_fences[swap_idx];
}

void ParallelCommandStream::end_submit(bool only_manual_wait) {
    this->ParallelCommandStream::end_submit(VK_NULL_HANDLE, VK_NULL_HANDLE);
}
void ParallelCommandStream::end_submit(const SubmitParameters *submit_params)
{
    int swap_idx = int(ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count);
    auto next_command_buffer = ref_data->async_command_buffers[swap_idx];
    assert(current_buffer == next_command_buffer);
    CHECK_VULKAN(vkEndCommandBuffer(next_command_buffer));

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &next_command_buffer;

    // Wait semaphores
    if (submit_params->num_wait_semaphores != 0) {
        submit_info.waitSemaphoreCount = submit_params->num_wait_semaphores;
        submit_info.pWaitSemaphores = submit_params->wait_semaphore_array.data();
        submit_info.pWaitDstStageMask = submit_params->wait_flag_array.data();
    }

    // Signal semaphores
    if (submit_params->num_signal_semaphore != 0) {
        submit_info.signalSemaphoreCount = submit_params->num_signal_semaphore;
        submit_info.pSignalSemaphores = submit_params->signal_semaphore_array.data();
    }

    CHECK_VULKAN(vkQueueSubmit(current_queue, 1, &submit_info, ref_data->async_fences[swap_idx]));
     
    // command buffer pending
    ++ref_data->async_command_buffer_cursor;
}

void ParallelCommandStream::end_submit(VkSemaphore wait_semaphore, VkSemaphore signal_semaphore) {
    int swap_idx = int(ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count);
    auto next_command_buffer = ref_data->async_command_buffers[swap_idx];
    assert(current_buffer == next_command_buffer);
    CHECK_VULKAN(vkEndCommandBuffer(next_command_buffer));

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkFlags waitDstStageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (wait_semaphore != VK_NULL_HANDLE) {
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &wait_semaphore;
        submit_info.pWaitDstStageMask = &waitDstStageFlags;
    }
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &next_command_buffer;
    if (signal_semaphore != VK_NULL_HANDLE) {
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &signal_semaphore;
    }

    CHECK_VULKAN(
        vkQueueSubmit(current_queue, 1, &submit_info, ref_data->async_fences[swap_idx]));

    // command buffer pending
    ++ref_data->async_command_buffer_cursor;
}

void ParallelCommandStream::wait_complete(int cursor_) {
    int64_t cursor = cursor_;
    if (cursor < 0) {
        cursor += ref_data->async_command_buffer_cursor;
        if (cursor < 0) return; // was not pending so far
    }
    
    CHECK_VULKAN(
        vkWaitForFences(ref_data->vkdevice
            , 1, &ref_data->async_fences[cursor % ref_data->async_command_buffer_count]
            , VK_TRUE, UINT64_MAX));
}

} // namespace
