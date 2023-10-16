// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include "ref_counted.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include "device_backend.h"
#include "error_io.h"

#define CHECK_VULKAN(FN)                                   \
    do {                                                   \
        VkResult r = FN;                                   \
        if (r != VK_SUCCESS) {                             \
            throw_error(#FN " failed with %d", int(r));    \
        }                                                  \
    } while (false)

#define IMAGE_BARRIER_DEFAULTS(img_mem_barrier) \
    img_mem_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; \
    img_mem_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; \
    img_mem_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; \
    img_mem_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; \
    img_mem_barrier.subresourceRange.baseMipLevel = 0; \
    img_mem_barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS; \
    img_mem_barrier.subresourceRange.baseArrayLayer = 0; \
    img_mem_barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS
#define IMAGE_BARRIER(img_mem_barrier) \
    VkImageMemoryBarrier img_mem_barrier{}; \
    IMAGE_BARRIER_DEFAULTS(img_mem_barrier)

#define IMAGE_BARRIER_DEFAULTS2(img_mem_barrier) \
    IMAGE_BARRIER_DEFAULTS(img_mem_barrier); \
    img_mem_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR; \

#define BUFFER_BARRIER_DEFAULTS(buf_mem_barrier) \
    buf_mem_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER; \
    buf_mem_barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; \
    buf_mem_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; \
    buf_mem_barrier.offset = 0; \
    buf_mem_barrier.size = VK_WHOLE_SIZE
#define BUFFER_BARRIER(buf_mem_barrier) \
    VkBufferMemoryBarrier buf_mem_barrier{}; \
    BUFFER_BARRIER_DEFAULTS(buf_mem_barrier)

#define DEFAULT_IMAGEBUFFER_PIPELINE_STAGES (\
      VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR \
    | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT \
    | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT \
)

struct SubmitParameters {
    // Wait semaphores
    std::vector<VkSemaphore> wait_semaphore_array;
    std::vector<VkPipelineStageFlags> wait_flag_array;
    uint64_t num_wait_semaphores;

    // Signal semaphores
    std::vector<VkSemaphore> signal_semaphore_array;
    uint64_t num_signal_semaphore;
};

namespace vkrt {

extern PFN_vkCmdTraceRaysKHR CmdTraceRaysKHR;
extern PFN_vkDestroyAccelerationStructureKHR DestroyAccelerationStructureKHR;
extern PFN_vkGetRayTracingShaderGroupHandlesKHR GetRayTracingShaderGroupHandlesKHR;
extern PFN_vkCmdWriteAccelerationStructuresPropertiesKHR
    CmdWriteAccelerationStructuresPropertiesKHR;
extern PFN_vkCreateAccelerationStructureKHR CreateAccelerationStructureKHR;
extern PFN_vkCmdBuildAccelerationStructuresKHR CmdBuildAccelerationStructuresKHR;
extern PFN_vkCmdCopyAccelerationStructureKHR CmdCopyAccelerationStructureKHR;
extern PFN_vkCreateRayTracingPipelinesKHR CreateRayTracingPipelinesKHR;
extern PFN_vkGetAccelerationStructureDeviceAddressKHR GetAccelerationStructureDeviceAddressKHR;
extern PFN_vkGetAccelerationStructureBuildSizesKHR GetAccelerationStructureBuildSizesKHR;
extern PFN_vkCreateDeferredOperationKHR CreateDeferredOperationKHR;
extern PFN_vkDeferredOperationJoinKHR DeferredOperationJoinKHR;
extern PFN_vkDestroyDeferredOperationKHR DestroyDeferredOperationKHR;

struct MemoryArena {
    struct Block {
        VkDeviceSize size = 0;
        uint32_t cursor = 0, freed = 0;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };
    std::vector<Block> types[VK_MAX_MEMORY_TYPES];
};

struct MemoryStatistics {
    size_t bytes_currently_allocated { 0 };
    size_t device_bytes_currently_allocated { 0 };
    size_t max_bytes_allocated { 0 };
    size_t max_device_bytes_allocated { 0 };
    size_t total_bytes_allocated { 0 };
    size_t total_allocation_count { 0 };

    size_t total_buffers_created { 0 };
    size_t total_images_created { 0 };
};

// Vulkan command stream (shadows interface name)
struct CommandStream;

// Type of command queues
enum class CommandQueueType
{
    // Main queue, Graphics + Compute
    Main,

    // Async queue compute only.
    Secondary
};

class Device : public ref_counted<Device> {
    VkDevice device = VK_NULL_HANDLE;

    struct shared_data {
        // Main queue
        VkQueue main_queue = VK_NULL_HANDLE;
        uint32_t main_queue_index = ~0;
        CommandStream *main_sync_commands = nullptr;
        CommandStream *main_async_commands = nullptr;

        // Secondary queue
        VkQueue secondary_queue = VK_NULL_HANDLE;
        uint32_t secondary_queue_index = ~0;
        CommandStream *secondary_sync_commands = nullptr;
        CommandStream *secondary_async_commands = nullptr;

        VkValidationCacheEXT validation_cache = VK_NULL_HANDLE;
        VkPipelineCache pipeline_cache = VK_NULL_HANDLE;

        VkInstance vk_instance = VK_NULL_HANDLE;
        VkPhysicalDevice vk_physical_device = VK_NULL_HANDLE;
#ifdef _DEBUG
        VkDebugUtilsMessengerEXT debug_messenger = VK_NULL_HANDLE;
#endif
        VkPhysicalDeviceMemoryProperties mem_props = {};
        std::vector<MemoryArena> memory_arenas;
        uint32_t deviceBufferAlignment = 0;
        uint32_t hostBufferAlignment = 0;
        uint32_t hostBufferAtomSize = 0;
        uint32_t allocationBlockSize = 0;
        uint32_t commonAllocationBlockSize = 0;
        uint32_t minAllocationBlockSize = 0;
        uint32_t maxAllocationBlockSize = 0;
        uint32_t nonCoherentAtomSize = 0;
        uint32_t subgroupSize = 0;
        VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props = {};
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_pipeline_props = {};

        char const* ray_tracing_extension = nullptr;
        char const* non_semantic_info_extension = nullptr;
        float nanoseconds_per_tick = 0;

        bool memory_type_is_device[VK_MAX_MEMORY_TYPES];

        // stats
        MemoryStatistics mem_stats;
        size_t maxAllocationBlockCount = 0;
    };
    template <class D> friend struct ref_counted;

    void release_resources();
public:
    Device(std::nullptr_t) : ref_counted(nullptr) { }
    Device(const std::vector<std::string> &instance_extensions = {},
           const std::vector<std::string> &logical_device_extensions = {},
           const char *device_override = nullptr);
    ~Device();

    Device* operator ->() { return this; } // shared_ptr<...>-like usage compatibility
    const Device* operator ->() const { return this; } // shared_ptr<...>-like usage compatibility
    Device& operator *() { return *this; }
    const Device& operator *() const { return *this; }

    VkDevice logical_device() { return device; }
    VkPhysicalDevice physical_device() const;
    VkInstance instance();
    
    VkValidationCacheEXT validation_cache();
    VkPipelineCache pipeline_cache();
    float nanoseconds_per_tick() const { return ref_data->nanoseconds_per_tick; }

    VkQueue main_queue();
    uint32_t main_queue_index() const;

    VkQueue secondary_queue();
    uint32_t secondary_queue_index() const;

    CommandStream *sync_command_stream(CommandQueueType type = CommandQueueType::Main);
    CommandStream *async_command_stream(CommandQueueType type = CommandQueueType::Main);
    void flush_sync_and_async_device_copies();

    void update_pipeline_cache();

    VkCommandPool make_command_pool(CommandQueueType type, VkCommandPoolCreateFlags flags = (VkCommandPoolCreateFlags)0);

    enum DefaultArenas {
        PersistentArena,
        DisplayArena,
        ScratchArena,
        DefaultArenaCount
    };
    uint32_t next_arena(size_t count = 1);
    uint32_t current_arena_index() const;
    uint32_t memory_type_index(uint32_t type_filter, VkMemoryPropertyFlags props) const;
    struct Allocation {
        VkDeviceMemory memory;
        uint32_t offset;
        uint16_t arena;
        uint16_t type;
    };
    Allocation alloc(uint32_t arena, size_t nbytes,
                     uint32_t type_filter, size_t alignment,
                     VkMemoryPropertyFlags props,
                     VkMemoryAllocateFlags allocFlags = 0,
                     size_t block_size_hint = 0,
                     float mem_priority = 1.0f);
    void free(uint32_t arena, uint32_t type, VkDeviceMemory &memory, size_t allocSize);
    size_t num_blocks_in_arena(uint32_t arena, uint32_t type = (uint32_t) ~0) const;
    std::vector<MemoryArena::Block> blocks_in_arena(uint32_t arena, VkMemoryPropertyFlags props) const;

    const VkPhysicalDeviceMemoryProperties&
        memory_properties() const;
    const VkPhysicalDeviceAccelerationStructurePropertiesKHR&
        acceleration_structure_properties() const;
    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR&
        raytracing_pipeline_properties() const;

    MemoryStatistics const& memory_statistics() const;

private:
    void make_instance(const std::vector<std::string> &extensions);
    void select_physical_device(const char *device_override);
    void make_logical_device(const std::vector<std::string> &extensions);
};

struct Buffer;
struct Texture2D;

struct ResourceStore {
    std::vector<Buffer> buffers;
    std::vector<Texture2D> textures;
};

struct CommandStream : ::CommandStream {
    VkCommandBuffer current_buffer = VK_NULL_HANDLE;
    VkQueue current_queue = VK_NULL_HANDLE;
    VkFence current_fence = VK_NULL_HANDLE;

    virtual void release_command_buffers() = 0;

    // holds buffers or textures after submission, until asynchronous completion
    virtual void hold_buffer(Buffer const& buf);
    virtual void hold_texture(Texture2D const& tex);
};

struct SyncCommandStream : CommandStream, ref_counted<SyncCommandStream> {
    // inherits current_buffer
    // inherits current_queue
    // inherits current_fence

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkDevice vkdevice = VK_NULL_HANDLE;

    SyncCommandStream(std::nullptr_t) : ref_counted(nullptr) { }
    SyncCommandStream(Device& device, CommandQueueType type);
    ~SyncCommandStream();
    void release_resources();

    void begin_record() override final;
    void end_submit(bool only_manual_wait = false) override final;
    void end_submit(const SubmitParameters* submit_params) override;
    void wait_complete(int cursor = -1) override final;
    void release_command_buffers() override final;

    void hold_buffer(Buffer const& buf) override final;
    void hold_texture(Texture2D const& tex) override final;
};

// note: don't use copies before beginning new recording
struct AsyncCommandStream : CommandStream, ref_counted<AsyncCommandStream> {
    static const int MAX_ASYNC_COMMAND_BUFFERS = 6;
    // inherits current_buffer
    // inherits current_queue
    // inherits current_fence

    struct shared_data {
        int async_command_buffer_count = 0;
        int async_command_buffer_cursor = 0;
        VkSemaphore async_command_timeline = VK_NULL_HANDLE;
        VkCommandBuffer async_command_buffers[MAX_ASYNC_COMMAND_BUFFERS] = { VK_NULL_HANDLE };
        ResourceStore async_resources[MAX_ASYNC_COMMAND_BUFFERS] = { };

        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkDevice vkdevice = VK_NULL_HANDLE;
    };

    AsyncCommandStream(std::nullptr_t) : ref_counted(nullptr) { }
    AsyncCommandStream(Device& device, CommandQueueType type, int async_command_buffer_count = MAX_ASYNC_COMMAND_BUFFERS);
    ~AsyncCommandStream();
    void release_resources();

    void begin_record() override final;
    void end_submit(bool only_manual_wait = false) override final;
    void end_submit(const SubmitParameters* submit_params) override;
    void wait_complete(int cursor = -1) override final;
    void release_command_buffers() override final;

    void hold_buffer(Buffer const& buf) override final;
    void hold_texture(Texture2D const& tex) override final;

    int current_index() const { return ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count; }
};

// note: don't use copies before beginning new recording
struct ParallelCommandStream : CommandStream, ref_counted<ParallelCommandStream> {
    static const int MAX_ASYNC_COMMAND_BUFFERS = 5;
    // inherits current_buffer
    // inherits current_queue
    // inherits current_fence

    struct shared_data {
        int async_command_buffer_count = 0;
        int64_t async_command_buffer_cursor = 0;
        VkCommandBuffer async_command_buffers[MAX_ASYNC_COMMAND_BUFFERS] = { VK_NULL_HANDLE };
        VkFence async_fences[MAX_ASYNC_COMMAND_BUFFERS] = { VK_NULL_HANDLE };

        VkCommandPool command_pool = VK_NULL_HANDLE;
        VkDevice vkdevice = VK_NULL_HANDLE;
    };

    ParallelCommandStream(std::nullptr_t) : ref_counted(nullptr) { }
    ParallelCommandStream(Device& device,  CommandQueueType type, int async_command_buffer_count = MAX_ASYNC_COMMAND_BUFFERS);
    ~ParallelCommandStream();
    void release_resources();

    void begin_record() override final;
    void end_submit(bool only_manual_wait = false) override final;
    void end_submit(const SubmitParameters * submit_params) override;
    void end_submit(VkSemaphore wait_semaphore, VkSemaphore signal_semaphore);
    void wait_complete(int cursor = -1) override final;
    void release_command_buffers() override final;

    int current_index() const { return int(ref_data->async_command_buffer_cursor % ref_data->async_command_buffer_count); }
};

enum MemorySourceArenas {
    NewArenaSource = 0x80000000,
};
struct MemorySource {
    Device *device = nullptr;
    int arena_idx = 0;
    float memory_priority = 1.0f;
    MemorySource(std::nullptr_t) { }
    MemorySource(Device &device, int arena_idx = Device::PersistentArena, float memory_priority = 1.0f);
    MemorySource(Device &device, MemorySourceArenas arena, float memory_priority = 1.0f);
};

enum ExtendedVkMemoryPropertyFlagBits : unsigned {
    ExtendedVkMemoryPropertyFlagsMask = 0xf0000000,
    VkMemoryPropertyFlagsMask = ~ExtendedVkMemoryPropertyFlagsMask,
    EXVK_MEMORY_PROPERTY_SCRATCH_SPACE_ALIGNMENT = 0x10000000,
    EXVK_MEMORY_PROPERTY_ZERO_BLOCK_OFFSET = 0x20000000,
};
typedef unsigned ExtendedVkMemoryPropertyFlags;

struct Buffer : ref_counted<Buffer> {
    VkBuffer buf = VK_NULL_HANDLE;
    struct shared_data; // see below, due to nested secondary buffer

    struct MemorySource;
    static VkBufferCreateInfo create_info(size_t nbytes, VkBufferUsageFlags usage);
    static Buffer make_buffer(MemorySource source,
                              size_t nbytes,
                              VkBufferUsageFlags usage,
                              ExtendedVkMemoryPropertyFlags mem_props,
                              int swap_buffer_count);

protected:
    struct init_tag { };
    // use make_buffer
    Buffer(init_tag);
    // Map a subset of the buffer starting at offset of some size
    void *map(size_t offset, size_t size);

public:
    Buffer(std::nullptr_t = nullptr) : ref_counted(nullptr) { }
    ~Buffer();
    void release_resources();

    Buffer* operator ->() { return this; } // shared_ptr<...>-like usage compatibility
    Buffer const* operator ->() const { return this; }
    Buffer& operator *() { return *this; }
    Buffer const& operator *() const { return *this; }
    operator VkBuffer() const { return buf; }

    static bool enable_blocked_alloc;
    static Buffer host(
        MemorySource,
        size_t nbytes,
        VkBufferUsageFlags usage,
        ExtendedVkMemoryPropertyFlags extra_mem_props = 0,
        int swap_buffer_count = 1);
    static Buffer device(
        MemorySource,
        size_t nbytes,
        VkBufferUsageFlags usage,
        ExtendedVkMemoryPropertyFlags extra_mem_props = 0,
        int swap_buffer_count = 1);

    static const MemorySource& NullMemorySource; // horrible workaround, incomplete type
    Buffer for_host(VkBufferUsageFlags usage,
        MemorySource const& source = NullMemorySource,
        ExtendedVkMemoryPropertyFlags extra_mem_props = 0);
    Buffer secondary_for_host(VkBufferUsageFlags usage,
        ExtendedVkMemoryPropertyFlags extra_mem_props = 0);
    Buffer new_secondary_for_host(VkBufferUsageFlags usage,
        ExtendedVkMemoryPropertyFlags extra_mem_props = 0);

    // Map the entire range of the buffer
    void *map();
    void unmap();
    void flush_all();
    void invalidate_all();

    size_t size() const;
    VkBuffer handle() const { return buf; }
    VkDeviceAddress device_address() const;
    size_t swap_offset() const;
    void cycle_swap(int swap_count);

    Buffer& secondary() const;
};

struct Buffer::shared_data {
    Buffer secondary = nullptr;

    uint16_t swap_idx = 0;
    uint16_t swap_count = 1;
    uint32_t swap_stride_padding = 0;
    size_t buf_size = 0;

    VkDeviceMemory mem = VK_NULL_HANDLE;
    size_t mem_size = 0;
    uint32_t mem_offset = 0;
    uint16_t arena_idx = 0;
    uint16_t type_idx = 0;

    VkBufferUsageFlags usage;
    ExtendedVkMemoryPropertyFlags mem_props;
    Device vkdevice = nullptr;
    bool host_visible = false;
    bool fully_mapped_and_undefined = false; // we auto-flush fully-mapped, non-coherent memory
};

struct Buffer::MemorySource : vkrt::MemorySource {
    Buffer reuse = nullptr;
    using vkrt::MemorySource::MemorySource;
    MemorySource(vkrt::MemorySource s) : vkrt::MemorySource(s) { }
};
inline Buffer::MemorySource reuse(MemorySource source, Buffer reuse) {
    Buffer::MemorySource r(source);
    r.reuse = reuse;
    return r;
}

struct Texture2D : public ref_counted<Texture2D> {
    VkImage image = VK_NULL_HANDLE;
    glm::ivec2 tdims = glm::ivec2(0);
    int mips = 0;
    int layers = 0;
    VkImageView view = VK_NULL_HANDLE;

    struct shared_data {
        VkImageLayout img_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        size_t mem_size = 0;
        uint32_t mem_offset = 0;
        uint16_t arena_idx = 0;
        uint16_t type_idx = 0;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkFormat img_format;
        VkImageUsageFlags usage;
        Device vkdevice = nullptr;
        std::vector<VkImageView> mipViews;
    };

protected:
    struct init_tag { };
    // use device()
    Texture2D(init_tag);
public:
    Texture2D(std::nullptr_t = nullptr) : ref_counted(nullptr) { }
    ~Texture2D();
    void release_resources();

    Texture2D* operator ->() { return this; } // shared_ptr<...>-like usage compatibility
    Texture2D const* operator ->() const { return this; }
    Texture2D& operator *() { return *this; }
    Texture2D const& operator *() const { return *this; }
    operator VkImage() const { return image; }

    // Note after creation image will be in the image_layout_undefined layout
    struct MemorySource;
    static Texture2D device(MemorySource source,
                            glm::ivec4 dims,
                            VkFormat img_format,
                            VkImageUsageFlags usage,
                            VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL);

    // Size of one pixel, in bytes
    size_t pixel_size() const;
    VkFormat pixel_format() const;
    glm::ivec2 dims() const;
    void allocate_mip_views();

    VkImage image_handle() const { return image; }
    VkImageView view_handle() const { return view; }
    VkImageView view_handle_mip(uint32_t mip_idx) const
    {
        assert(ref_data->mipViews.size() > mip_idx);
        return ref_data->mipViews[mip_idx];
    }

    VkImageMemoryBarrier await_color(VkAccessFlags dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) const {
        IMAGE_BARRIER(img_mem_barrier);
        img_mem_barrier.image = image;
        img_mem_barrier.newLayout = img_mem_barrier.oldLayout = ref_data->img_layout;
        img_mem_barrier.dstAccessMask = dstAccessMask;
        return img_mem_barrier;
    }
    void layout_invalidate() {
        ref_data->img_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    VkImageMemoryBarrier transition_color(VkImageLayout newLayout
        , VkAccessFlags dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT) {
        IMAGE_BARRIER(img_mem_barrier);
        img_mem_barrier.image = image;
        img_mem_barrier.oldLayout = ref_data->img_layout;
        // VK_IMAGE_LAYOUT_UNDEFINED is not a valid destination layout, this is the way we found to add a barrier without changing the layout.
        img_mem_barrier.newLayout = newLayout != VK_IMAGE_LAYOUT_UNDEFINED? newLayout : ref_data->img_layout;
        img_mem_barrier.dstAccessMask = dstAccessMask;
        ref_data->img_layout = newLayout;
        return img_mem_barrier;
    }
    static VkImageSubresourceLayers color_subresource(int mip = 0, int baseLayer = 0, int layerCount = 1) {
        VkImageSubresourceLayers subresource = {};
        subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresource.mipLevel = 0;
        subresource.baseArrayLayer = 0;
        subresource.layerCount = 1;
        return subresource;
    }
};

inline VkImageSubresourceRange subresource_range(VkImageSubresourceLayers layers) {
    VkImageSubresourceRange subresources = {};
    subresources.aspectMask = layers.aspectMask;
    subresources.baseMipLevel = layers.mipLevel;
    subresources.levelCount = 1;
    subresources.baseArrayLayer = layers.baseArrayLayer;
    subresources.layerCount = layers.layerCount;
    return subresources;
}

struct Texture2D::MemorySource : vkrt::MemorySource {
    Texture2D reuse = nullptr;
    Texture2D alias = nullptr;
    using vkrt::MemorySource::MemorySource;
    MemorySource(vkrt::MemorySource s) : vkrt::MemorySource(s) {}
};
inline Texture2D::MemorySource reuse(MemorySource source, Texture2D reuse)
{
    Texture2D::MemorySource r(source);
    r.reuse = reuse;
    return r;
}
inline Texture2D::MemorySource alias(MemorySource source, Texture2D alias)
{
    Texture2D::MemorySource r(source);
    r.alias = alias;
    return r;
}

struct Texture3D : public ref_counted<Texture3D> {
    VkImage image = VK_NULL_HANDLE;
    glm::ivec3 tdims = glm::ivec3(0);
    int mips = 0;
    int layers = 0;
    VkImageView view = VK_NULL_HANDLE;

    struct shared_data {
        VkImageLayout img_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        size_t mem_size = 0;
        uint32_t mem_offset = 0;
        uint16_t arena_idx = 0;
        uint16_t type_idx = 0;
        VkDeviceMemory mem = VK_NULL_HANDLE;
        VkFormat img_format;
        VkImageUsageFlags usage;
        Device vkdevice = nullptr;
    };

protected:
    struct init_tag {
    };
    // use device()
    Texture3D(init_tag);

public:
    Texture3D(std::nullptr_t = nullptr) : ref_counted(nullptr) {}
    ~Texture3D();
    void release_resources();

    Texture3D *operator->()
    {
        return this;
    }  // shared_ptr<...>-like usage compatibility
    Texture3D const *operator->() const
    {
        return this;
    }
    Texture3D &operator*()
    {
        return *this;
    }
    Texture3D const &operator*() const
    {
        return *this;
    }
    operator VkImage() const
    {
        return image;
    }

    // Note after creation image will be in the image_layout_undefined layout
    struct MemorySource;
    static Texture3D device(MemorySource source,
                            glm::ivec4 dims,
                            VkFormat img_format,
                            VkImageUsageFlags usage);

    // Size of one pixel, in bytes
    size_t pixel_size() const;
    VkFormat pixel_format() const;
    glm::ivec3 dims() const;

    VkImage image_handle() const
    {
        return image;
    }

    VkImageView view_handle() const
    {
        return view;
    }

    VkImageMemoryBarrier await_color(
        VkAccessFlags dstAccessMask = VK_ACCESS_MEMORY_READ_BIT |
                                      VK_ACCESS_MEMORY_WRITE_BIT) const
    {
        IMAGE_BARRIER(img_mem_barrier);
        img_mem_barrier.image = image;
        img_mem_barrier.newLayout = img_mem_barrier.oldLayout = ref_data->img_layout;
        img_mem_barrier.dstAccessMask = dstAccessMask;
        return img_mem_barrier;
    }
    void layout_invalidate()
    {
        ref_data->img_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    VkImageMemoryBarrier transition_color(
        VkImageLayout newLayout,
        VkAccessFlags dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT)
    {
        IMAGE_BARRIER(img_mem_barrier);
        img_mem_barrier.image = image;
        img_mem_barrier.oldLayout = ref_data->img_layout;
        img_mem_barrier.newLayout = newLayout;
        img_mem_barrier.dstAccessMask = dstAccessMask;
        ref_data->img_layout = newLayout;
        return img_mem_barrier;
    }
    static VkImageSubresourceLayers color_subresource(int mip = 0,
                                                      int baseLayer = 0,
                                                      int layerCount = 1)
    {
        VkImageSubresourceLayers copy_subresource = {};
        copy_subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy_subresource.mipLevel = 0;
        copy_subresource.baseArrayLayer = 0;
        copy_subresource.layerCount = 1;
        return copy_subresource;
    }
};

struct Texture3D::MemorySource : vkrt::MemorySource {
    Texture3D reuse = nullptr;
    Texture3D alias = nullptr;
    using vkrt::MemorySource::MemorySource;
    MemorySource(vkrt::MemorySource s) : vkrt::MemorySource(s) {}
};
inline Texture3D::MemorySource reuse(MemorySource source, Texture3D reuse)
{
    Texture3D::MemorySource r(source);
    r.reuse = reuse;
    return r;
}
inline Texture3D::MemorySource alias(MemorySource source, Texture3D alias)
{
    Texture3D::MemorySource r(source);
    r.alias = alias;
    return r;
}


template <int MaxBuffers, int MaxImages>
struct MemoryBarriers {
    int buffer_idx = 0;
    int image_idx = 0;
    VkPipelineStageFlags src_stages = 0;
    VkPipelineStageFlags dst_stages = 0;
    VkBufferMemoryBarrier buffer_barriers[MaxBuffers];
    VkImageMemoryBarrier image_barriers[MaxImages];
    void add(VkPipelineStageFlags dst_stages, VkBufferMemoryBarrier barrier) {
        assert(buffer_idx < MaxBuffers);
        this->dst_stages |= dst_stages;
        this->buffer_barriers[buffer_idx++] = barrier;
    }
    void add(VkPipelineStageFlags dst_stages, VkImageMemoryBarrier barrier) {
        assert(image_idx < MaxImages);
        this->dst_stages |= dst_stages;
        this->image_barriers[image_idx++] = barrier;
    }
    void set(VkCommandBuffer cmd_buf, VkPipelineStageFlags src_stages, VkPipelineStageFlags dst_stages = 0) {
        assert(src_stages | this->src_stages);
        assert(dst_stages | this->dst_stages);
        vkCmdPipelineBarrier(cmd_buf,
                            src_stages | this->src_stages,
                            dst_stages | this->dst_stages,
                            0,
                            0, nullptr,
                            uint32_t(buffer_idx), buffer_barriers,
                            uint32_t(image_idx), image_barriers);
    }
};

struct ShaderModule : public ref_counted<ShaderModule> {
    VkShaderModule module = VK_NULL_HANDLE;

    struct shared_data {
        VkDevice vkdevice = VK_NULL_HANDLE;
    };

protected:
    // use make_buffer
    ShaderModule() = default;
public:
    ShaderModule(std::nullptr_t) : ref_counted(nullptr) { }
    ShaderModule(Device &device, const uint32_t *code, size_t code_size);
    ShaderModule(Device &device, std::vector<char> const& code);
    ~ShaderModule();
    void release_resources();

    ShaderModule* operator ->() { return this; } // shared_ptr<...>-like usage compatibility
    ShaderModule const* operator ->() const { return this; }
    ShaderModule& operator *() { return *this; }
    ShaderModule const& operator *() const { return *this; }
    operator VkShaderModule() const { return module; }

};

struct DescriptorSetLayoutBuilder {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    std::vector<VkDescriptorBindingFlagsEXT> binding_ext_flags;

    uint32_t default_ext_flags = 0;
    DescriptorSetLayoutBuilder(uint32_t default_ext_flags = 0)
        : default_ext_flags(default_ext_flags) { }

    DescriptorSetLayoutBuilder &add_binding(uint32_t binding,
                                            uint32_t count,
                                            VkDescriptorType type,
                                            uint32_t stage_flags,
                                            uint32_t ext_flags = 0);

    VkDescriptorSetLayout build(Device &device) const;
    VkDescriptorPool build_compatible_pool(Device &device, int multiplicity) const;
};

struct WriteDescriptorInfo {
    VkDescriptorSet dst_set = VK_NULL_HANDLE;
    uint32_t binding = 0;
    uint32_t count = 0;
    VkDescriptorType type;
    size_t as_index = -1;
    size_t img_index = -1;
    size_t buf_index = -1;
};

struct DescriptorSetUpdater {
    std::vector<WriteDescriptorInfo> writes;
    std::vector<VkWriteDescriptorSetAccelerationStructureKHR> accel_structs;
    std::vector<VkDescriptorImageInfo> images;
    std::vector<VkDescriptorBufferInfo> buffers;

public:
    DescriptorSetUpdater &write_acceleration_structures(
        VkDescriptorSet set, uint32_t binding,
        VkAccelerationStructureKHR const* bvh, int count);

    DescriptorSetUpdater &write_storage_image(VkDescriptorSet set,
                                              uint32_t binding,
                                              const Texture2D &img);
    DescriptorSetUpdater &write_storage_image_mip(VkDescriptorSet set,
                                              uint32_t binding,
                                              const Texture2D &img, uint32_t mip);
    DescriptorSetUpdater &write_storage_image(VkDescriptorSet set,
                                              uint32_t binding,
                                              const Texture3D &img);
    DescriptorSetUpdater &write_storage_image_views(VkDescriptorSet set,
                                              uint32_t binding,
                                              const VkImageView *imgs, int count);

    DescriptorSetUpdater &write_ubo(VkDescriptorSet set,
                                    uint32_t binding,
                                    const Buffer &buf);

    DescriptorSetUpdater &write_ssbo(VkDescriptorSet set,
                                     uint32_t binding,
                                     const Buffer &buf);

    DescriptorSetUpdater &write_ssbo_array(VkDescriptorSet set,
                                           uint32_t binding,
                                           const Buffer *bufs, int count);

    DescriptorSetUpdater &write_combined_sampler_array(
        VkDescriptorSet set,
        uint32_t binding,
        const std::vector<Texture2D> &textures,
        const std::vector<VkSampler> &samplers);

    DescriptorSetUpdater &write_combined_sampler(VkDescriptorSet set,
        uint32_t binding,
        const Texture2D &texture,
        VkSampler sampler);

    DescriptorSetUpdater &write_combined_sampler(VkDescriptorSet set,
        uint32_t binding,
        const Texture3D &texture,
        VkSampler sampler);

    // Commit the writes to the descriptor sets
    void update(Device &device);
    void reset();
};

VkResult build_compute_pipeline(Device &device, VkPipeline* pipeline, VkPipelineLayout layout, const ShaderModule &shader, char const * entry_point = "main");
void get_workgroup_size(char const* const* defines, int* x, int* y, int* z);

} // namespace

struct ComputeDeviceVulkan : ComputeDevice {
    vkrt::Device device;
    ComputeDeviceVulkan(vkrt::Device const& device);

    CommandStream* sync_command_stream() override;
    std::unique_ptr<GpuBuffer> create_uniform_buffer(size_t size) override;
    std::unique_ptr<GpuBuffer> create_buffer(size_t size) override;
    std::unique_ptr<ComputePipeline> create_pipeline() override;
};
