#include <nozomi/rhi/vulkan/vulkan_device.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(NOZOMI_HAS_VULKAN_SDK) && NOZOMI_HAS_VULKAN_SDK && __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#define NOZOMI_VULKAN_BOOTSTRAP_ENABLED 1
#else
#define NOZOMI_VULKAN_BOOTSTRAP_ENABLED 0
#endif

#if defined(NOZOMI_HAS_VMA) && NOZOMI_HAS_VMA && \
    defined(NOZOMI_HAS_VULKAN_SDK) && NOZOMI_HAS_VULKAN_SDK && \
    __has_include(<vk_mem_alloc.h>)
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#define NOZOMI_VMA_ENABLED 1
#else
#define NOZOMI_VMA_ENABLED 0
#endif

namespace Nozomi::RHI
{
namespace
{
[[nodiscard]] constexpr std::size_t queue_slot(const QueueType queue) noexcept
{
    switch (queue)
    {
    case QueueType::k_graphics:
        return 0;
    case QueueType::k_compute:
        return 1;
    case QueueType::k_transfer:
        return 2;
    }

    return 0;
}

class UnavailableMemoryAllocator final : public MemoryAllocator
{
public:
    explicit UnavailableMemoryAllocator(std::string reason)
        : reason_(std::move(reason))
    {
    }

    [[nodiscard]] MemoryAllocatorBackend backend() const noexcept override
    {
        return MemoryAllocatorBackend::k_vma;
    }

    [[nodiscard]] AllocationHandle allocate(const AllocationDesc&) override
    {
        throw std::runtime_error(reason_);
    }

    void free(AllocationHandle) override
    {
    }

    void begin_frame(std::uint64_t) override
    {
    }

    void end_frame(std::uint64_t) override
    {
    }

    void trim() override
    {
    }

    [[nodiscard]] MemoryBudget budget() const override
    {
        return {};
    }

private:
    std::string reason_;
};

class Phase1CommandBuffer final : public CommandBuffer
{
public:
    explicit Phase1CommandBuffer(CommandBufferDesc desc)
        : desc_(desc)
    {
    }

    [[nodiscard]] QueueType queue_type() const noexcept override
    {
        return desc_.queue;
    }

    [[nodiscard]] CommandBufferLevel level() const noexcept override
    {
        return desc_.level;
    }

    void begin() override
    {
        recording_ = true;
    }

    void end() override
    {
        recording_ = false;
        label_depth_ = 0;
    }

    void reset() override
    {
        recording_ = false;
        label_depth_ = 0;
    }

    void begin_label(std::string_view) override
    {
        if (recording_)
        {
            ++label_depth_;
        }
    }

    void end_label() override
    {
        if (label_depth_ > 0)
        {
            --label_depth_;
        }
    }

    void barrier(const GlobalBarrier&) override
    {
    }

private:
    CommandBufferDesc desc_ {};
    bool recording_ = false;
    std::uint32_t label_depth_ = 0;
};

#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED
[[nodiscard]] const char* vk_result_name(const VkResult result) noexcept
{
    switch (result)
    {
    case VK_SUCCESS:
        return "VK_SUCCESS";
    case VK_NOT_READY:
        return "VK_NOT_READY";
    case VK_TIMEOUT:
        return "VK_TIMEOUT";
    case VK_EVENT_SET:
        return "VK_EVENT_SET";
    case VK_EVENT_RESET:
        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
        return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
        return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
        return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
        return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
        return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_DEVICE_LOST:
        return "VK_ERROR_DEVICE_LOST";
    default:
        return "VK_ERROR_UNKNOWN";
    }
}

[[nodiscard]] VkQueueFlags required_flags_for_queue(const QueueType queue) noexcept
{
    switch (queue)
    {
    case QueueType::k_graphics:
        return VK_QUEUE_GRAPHICS_BIT;
    case QueueType::k_compute:
        return VK_QUEUE_COMPUTE_BIT;
    case QueueType::k_transfer:
        return VK_QUEUE_TRANSFER_BIT;
    }

    return VK_QUEUE_GRAPHICS_BIT;
}

[[nodiscard]] bool prefer_dedicated_queue(const QueueType queue, const VkQueueFlags flags) noexcept
{
    switch (queue)
    {
    case QueueType::k_graphics:
        return false;
    case QueueType::k_compute:
        return (flags & VK_QUEUE_COMPUTE_BIT) != 0 && (flags & VK_QUEUE_GRAPHICS_BIT) == 0;
    case QueueType::k_transfer:
        return (flags & VK_QUEUE_TRANSFER_BIT) != 0 &&
               (flags & VK_QUEUE_GRAPHICS_BIT) == 0 &&
               (flags & VK_QUEUE_COMPUTE_BIT) == 0;
    }

    return false;
}

[[nodiscard]] std::uint32_t choose_queue_family_index(
    const QueueType queue,
    const std::vector<VkQueueFamilyProperties>& families) noexcept
{
    const auto required_flags = required_flags_for_queue(queue);

    for (std::uint32_t family_index = 0; family_index < families.size(); ++family_index)
    {
        const auto flags = families[family_index].queueFlags;
        if ((flags & required_flags) == required_flags && prefer_dedicated_queue(queue, flags))
        {
            return family_index;
        }
    }

    for (std::uint32_t family_index = 0; family_index < families.size(); ++family_index)
    {
        const auto flags = families[family_index].queueFlags;
        if ((flags & required_flags) == required_flags)
        {
            return family_index;
        }
    }

    return std::numeric_limits<std::uint32_t>::max();
}
#endif

#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED && NOZOMI_VMA_ENABLED
[[nodiscard]] VmaMemoryUsage to_vma_usage(const MemoryDomain domain) noexcept
{
    switch (domain)
    {
    case MemoryDomain::k_device_local:
        return VMA_MEMORY_USAGE_GPU_ONLY;
    case MemoryDomain::k_upload:
        return VMA_MEMORY_USAGE_CPU_TO_GPU;
    case MemoryDomain::k_readback:
        return VMA_MEMORY_USAGE_GPU_TO_CPU;
    }

    return VMA_MEMORY_USAGE_UNKNOWN;
}
#endif

#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED && NOZOMI_VMA_ENABLED
class VmaMemoryAllocator final : public MemoryAllocator
{
public:
    VmaMemoryAllocator(
        const VkInstance instance,
        const VkPhysicalDevice physical_device,
        const VkDevice device,
        const DeviceMemoryAllocatorDesc& desc,
        const VkAllocationCallbacks* allocation_callbacks)
        : physical_device_(physical_device)
        , enable_defragmentation_(desc.enable_defragmentation)
    {
        if (desc.backend != MemoryAllocatorBackend::k_vma)
        {
            throw std::runtime_error("Nozomi Vulkan devices only support the VMA allocator backend.");
        }

        VmaAllocatorCreateInfo create_info {};
        create_info.instance = instance;
        create_info.physicalDevice = physical_device;
        create_info.device = device;
        create_info.pAllocationCallbacks = allocation_callbacks;

        const auto result = vmaCreateAllocator(&create_info, &allocator_);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error(
                std::string("Failed to create VMA allocator: ") + vk_result_name(result));
        }
    }

    ~VmaMemoryAllocator() override
    {
        for (const auto& [id, allocation] : allocations_)
        {
            (void)id;
            vmaFreeMemory(allocator_, allocation);
        }

        allocations_.clear();

        if (allocator_ != nullptr)
        {
            vmaDestroyAllocator(allocator_);
            allocator_ = nullptr;
        }
    }

    [[nodiscard]] MemoryAllocatorBackend backend() const noexcept override
    {
        return MemoryAllocatorBackend::k_vma;
    }

    [[nodiscard]] AllocationHandle allocate(const AllocationDesc& desc) override
    {
        if (desc.size == 0)
        {
            return {};
        }

        VkMemoryRequirements requirements {};
        requirements.size = static_cast<VkDeviceSize>(desc.size);
        requirements.alignment = static_cast<VkDeviceSize>(std::max<std::uint64_t>(desc.alignment, 1));
        requirements.memoryTypeBits = desc.memory_type_bits;

        VmaAllocationCreateInfo create_info {};
        create_info.usage = to_vma_usage(desc.domain);

        if (desc.dedicated)
        {
            create_info.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }

        VmaAllocation allocation = nullptr;
        const auto result = vmaAllocateMemory(allocator_, &requirements, &create_info, &allocation, nullptr);
        if (result != VK_SUCCESS)
        {
            return {};
        }

        const AllocationHandle handle {next_allocation_id_++};
        allocations_.emplace(handle.id, allocation);
        return handle;
    }

    void free(const AllocationHandle allocation) override
    {
        const auto it = allocations_.find(allocation.id);
        if (it == allocations_.end())
        {
            return;
        }

        vmaFreeMemory(allocator_, it->second);
        allocations_.erase(it);
    }

    void begin_frame(const std::uint64_t frame_index) override
    {
        vmaSetCurrentFrameIndex(allocator_, static_cast<std::uint32_t>(frame_index));
    }

    void end_frame(const std::uint64_t) override
    {
    }

    void trim() override
    {
        if (!enable_defragmentation_)
        {
            return;
        }

        // Phase 1 keeps VMA in charge of sub-allocation policy; explicit defrag orchestration comes later.
    }

    [[nodiscard]] MemoryBudget budget() const override
    {
        VkPhysicalDeviceMemoryProperties memory_properties {};
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);

        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets {};
        vmaGetBudget(allocator_, budgets.data());

        MemoryBudget total {};
        for (std::uint32_t heap_index = 0; heap_index < memory_properties.memoryHeapCount; ++heap_index)
        {
            total.bytes_used += budgets[heap_index].usage;
            total.bytes_reserved += budgets[heap_index].statistics.blockBytes;
            total.bytes_budget += budgets[heap_index].budget;
        }

        return total;
    }

private:
    VkPhysicalDevice physical_device_ = nullptr;
    VmaAllocator allocator_ = nullptr;
    bool enable_defragmentation_ = true;
    std::uint64_t next_allocation_id_ = 1;
    std::unordered_map<std::uint64_t, VmaAllocation> allocations_ {};
};
#endif
} // namespace

class VulkanDevice::Impl
{
public:
    explicit Impl(const VulkanDeviceDesc& desc)
        : memory_allocator_(std::make_unique<UnavailableMemoryAllocator>("Vulkan bootstrap has not run yet."))
    {
        if (!desc.base.queues.empty())
        {
            queue_requests_.assign(desc.base.queues.begin(), desc.base.queues.end());
        }

        if (!desc.instance_layers.empty())
        {
            instance_layers_.assign(desc.instance_layers.begin(), desc.instance_layers.end());
        }

        if (!desc.instance_extensions.empty())
        {
            instance_extensions_.assign(desc.instance_extensions.begin(), desc.instance_extensions.end());
        }

        if (!desc.device_extensions.empty())
        {
            device_extensions_.assign(desc.device_extensions.begin(), desc.device_extensions.end());
        }

        desc_ = desc;
        desc_.base.queues = std::span<const QueueRequest> {queue_requests_.data(), queue_requests_.size()};
        desc_.instance_layers = std::span<const char* const> {instance_layers_.data(), instance_layers_.size()};
        desc_.instance_extensions =
            std::span<const char* const> {instance_extensions_.data(), instance_extensions_.size()};
        desc_.device_extensions =
            std::span<const char* const> {device_extensions_.data(), device_extensions_.size()};

        bootstrap();
    }

    ~Impl()
    {
        memory_allocator_.reset();
        destroy_bootstrap_handles();
    }

    [[nodiscard]] const DeviceDesc& desc() const noexcept
    {
        return desc_.base;
    }

    [[nodiscard]] MemoryAllocator& memory_allocator() noexcept
    {
        return *memory_allocator_;
    }

    [[nodiscard]] const MemoryAllocator& memory_allocator() const noexcept
    {
        return *memory_allocator_;
    }

    [[nodiscard]] MemoryBudget memory_budget() const
    {
        return memory_allocator_->budget();
    }

    [[nodiscard]] std::string_view adapter_name() const noexcept
    {
        return adapter_name_;
    }

    [[nodiscard]] VulkanInitializationStatus initialization_status() const noexcept
    {
        return {initialization_state_, initialization_detail_};
    }

    [[nodiscard]] bool is_ready() const noexcept
    {
        return initialization_state_ == VulkanInitializationState::k_ready;
    }

    void begin_frame(const std::uint64_t frame_index)
    {
        memory_allocator_->begin_frame(frame_index);
    }

    void end_frame(const std::uint64_t frame_index)
    {
        memory_allocator_->end_frame(frame_index);
    }

    void collect_garbage()
    {
        memory_allocator_->trim();
    }

    void wait_idle()
    {
#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED
        if (!is_ready() || device_ == nullptr)
        {
            return;
        }

        static_cast<void>(vkDeviceWaitIdle(device_));
#endif
    }

    std::array<std::uint64_t, 3> timelines_ {};

private:
    void bootstrap()
    {
        if (desc_.base.allocator.backend != MemoryAllocatorBackend::k_vma)
        {
            fail("Nozomi Vulkan devices forbid handwritten allocation backends. Use VMA.");
            return;
        }

#if !NOZOMI_VULKAN_BOOTSTRAP_ENABLED
        fail("Vulkan SDK headers or linkage are unavailable, so Vulkan bootstrap remains disabled.");
        return;
#elif !NOZOMI_VMA_ENABLED
        fail(
            "VMA was not found in third_party/VulkanMemoryAllocator/include. "
            "The backend will not fall back to a bespoke Vulkan allocator.");
        return;
#else
        if (!create_instance())
        {
            return;
        }

        if (!select_physical_device())
        {
            return;
        }

        if (!create_logical_device())
        {
            return;
        }

        if (!create_memory_allocator())
        {
            return;
        }

        initialization_state_ = VulkanInitializationState::k_ready;
        initialization_detail_ = "ok";
#endif
    }

    void fail(std::string detail)
    {
        initialization_state_ = VulkanInitializationState::k_failed;
        initialization_detail_ = std::move(detail);
        adapter_name_.clear();

        memory_allocator_ = std::make_unique<UnavailableMemoryAllocator>(initialization_detail_);
        destroy_bootstrap_handles();
    }

    void destroy_bootstrap_handles()
    {
#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED
        for (auto& queue : queues_)
        {
            if (queue.command_pool != nullptr && device_ != nullptr)
            {
                vkDestroyCommandPool(device_, queue.command_pool, desc_.allocation_callbacks);
                queue.command_pool = nullptr;
            }

            queue.handle = nullptr;
            queue.family_index = std::numeric_limits<std::uint32_t>::max();
        }

        if (device_ != nullptr)
        {
            vkDestroyDevice(device_, desc_.allocation_callbacks);
            device_ = nullptr;
        }

        physical_device_ = nullptr;

        if (instance_ != nullptr)
        {
            vkDestroyInstance(instance_, desc_.allocation_callbacks);
            instance_ = nullptr;
        }
#endif
    }

#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED && NOZOMI_VMA_ENABLED
    bool create_instance()
    {
        VkApplicationInfo application_info {};
        application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        application_info.pApplicationName = "NozomiEngine";
        application_info.pEngineName = "NozomiEngine";
        application_info.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &application_info;
        create_info.enabledLayerCount = static_cast<std::uint32_t>(instance_layers_.size());
        create_info.ppEnabledLayerNames = instance_layers_.data();
        create_info.enabledExtensionCount = static_cast<std::uint32_t>(instance_extensions_.size());
        create_info.ppEnabledExtensionNames = instance_extensions_.data();

        const auto result = vkCreateInstance(&create_info, desc_.allocation_callbacks, &instance_);
        if (result != VK_SUCCESS)
        {
            fail(std::string("Failed to create Vulkan instance: ") + vk_result_name(result));
            return false;
        }

        return true;
    }

    bool select_physical_device()
    {
        std::uint32_t device_count = 0;
        auto result = vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
        if (result != VK_SUCCESS || device_count == 0)
        {
            fail("Failed to enumerate Vulkan physical devices.");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(device_count);
        result = vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
        if (result != VK_SUCCESS)
        {
            fail(std::string("Failed to fetch Vulkan physical devices: ") + vk_result_name(result));
            return false;
        }

        auto preferred = devices.front();
        VkPhysicalDeviceProperties preferred_properties {};
        vkGetPhysicalDeviceProperties(preferred, &preferred_properties);

        for (const auto candidate : devices)
        {
            VkPhysicalDeviceProperties properties {};
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            {
                preferred = candidate;
                preferred_properties = properties;
                break;
            }
        }

        physical_device_ = preferred;
        adapter_name_ = preferred_properties.deviceName;

        std::uint32_t family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &family_count, nullptr);
        queue_families_.resize(family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physical_device_,
            &family_count,
            queue_families_.data());

        const QueueRequest default_graphics_queue {};
        const auto requested_queues = desc_.base.queues.empty()
            ? std::span<const QueueRequest> {&default_graphics_queue, 1}
            : desc_.base.queues;

        for (const auto& request : requested_queues)
        {
            auto& queue = queues_[queue_slot(request.type)];
            queue.family_index = choose_queue_family_index(request.type, queue_families_);
            if (queue.family_index == std::numeric_limits<std::uint32_t>::max())
            {
                fail("Could not find a Vulkan queue family matching the requested device queues.");
                return false;
            }
        }

        return true;
    }

    bool create_logical_device()
    {
        std::vector<std::uint32_t> family_indices;
        family_indices.reserve(queues_.size());

        for (const auto& queue : queues_)
        {
            if (queue.family_index != std::numeric_limits<std::uint32_t>::max())
            {
                family_indices.push_back(queue.family_index);
            }
        }

        std::sort(family_indices.begin(), family_indices.end());
        family_indices.erase(std::unique(family_indices.begin(), family_indices.end()), family_indices.end());

        std::vector<std::vector<float>> priorities(family_indices.size(), std::vector<float>(1, 1.0f));
        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        queue_create_infos.reserve(family_indices.size());

        for (std::size_t index = 0; index < family_indices.size(); ++index)
        {
            VkDeviceQueueCreateInfo queue_info {};
            queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info.queueFamilyIndex = family_indices[index];
            queue_info.queueCount = static_cast<std::uint32_t>(priorities[index].size());
            queue_info.pQueuePriorities = priorities[index].data();
            queue_create_infos.push_back(queue_info);
        }

        VkPhysicalDeviceFeatures enabled_features {};
        VkDeviceCreateInfo create_info {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = static_cast<std::uint32_t>(queue_create_infos.size());
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.enabledExtensionCount = static_cast<std::uint32_t>(device_extensions_.size());
        create_info.ppEnabledExtensionNames = device_extensions_.data();
        create_info.pEnabledFeatures = &enabled_features;

        const auto result = vkCreateDevice(physical_device_, &create_info, desc_.allocation_callbacks, &device_);
        if (result != VK_SUCCESS)
        {
            fail(std::string("Failed to create Vulkan logical device: ") + vk_result_name(result));
            return false;
        }

        for (auto& queue : queues_)
        {
            if (queue.family_index == std::numeric_limits<std::uint32_t>::max())
            {
                continue;
            }

            vkGetDeviceQueue(device_, queue.family_index, 0, &queue.handle);

            VkCommandPoolCreateInfo command_pool_info {};
            command_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            command_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            command_pool_info.queueFamilyIndex = queue.family_index;

            const auto pool_result = vkCreateCommandPool(
                device_,
                &command_pool_info,
                desc_.allocation_callbacks,
                &queue.command_pool);
            if (pool_result != VK_SUCCESS)
            {
                fail(std::string("Failed to create Vulkan command pool: ") + vk_result_name(pool_result));
                return false;
            }
        }

        return true;
    }

    bool create_memory_allocator()
    {
        try
        {
            memory_allocator_ = std::make_unique<VmaMemoryAllocator>(
                instance_,
                physical_device_,
                device_,
                desc_.base.allocator,
                desc_.allocation_callbacks);
        }
        catch (const std::exception& exception)
        {
            fail(exception.what());
            return false;
        }

        return true;
    }
#endif

    struct QueueContext
    {
        std::uint32_t family_index = std::numeric_limits<std::uint32_t>::max();
        VkQueue handle = nullptr;
        VkCommandPool command_pool = nullptr;
    };

    VulkanDeviceDesc desc_ {};
    std::vector<QueueRequest> queue_requests_ {};
    std::vector<const char*> instance_layers_ {};
    std::vector<const char*> instance_extensions_ {};
    std::vector<const char*> device_extensions_ {};
    VulkanInitializationState initialization_state_ = VulkanInitializationState::k_uninitialized;
    std::string initialization_detail_ {};
    std::string adapter_name_ {};
    VkInstance instance_ = nullptr;
    VkPhysicalDevice physical_device_ = nullptr;
    VkDevice device_ = nullptr;
#if NOZOMI_VULKAN_BOOTSTRAP_ENABLED
    std::vector<VkQueueFamilyProperties> queue_families_ {};
#endif
    std::array<QueueContext, 3> queues_ {};
    std::unique_ptr<MemoryAllocator> memory_allocator_ {};
};

VulkanDevice::VulkanDevice(const VulkanDeviceDesc& desc)
    : impl_(std::make_unique<Impl>(desc))
{
}

VulkanDevice::~VulkanDevice() = default;

Backend VulkanDevice::backend() const noexcept
{
    return Backend::k_vulkan;
}

std::string_view VulkanDevice::adapter_name() const noexcept
{
    return impl_->adapter_name();
}

const DeviceDesc& VulkanDevice::desc() const noexcept
{
    return impl_->desc();
}

MemoryAllocator& VulkanDevice::memory_allocator() noexcept
{
    return impl_->memory_allocator();
}

const MemoryAllocator& VulkanDevice::memory_allocator() const noexcept
{
    return impl_->memory_allocator();
}

MemoryBudget VulkanDevice::memory_budget() const
{
    return impl_->memory_budget();
}

std::unique_ptr<CommandBuffer> VulkanDevice::create_command_buffer(const CommandBufferDesc& desc)
{
    if (!impl_->is_ready())
    {
        throw std::runtime_error(std::string(impl_->initialization_status().detail));
    }

    return std::make_unique<Phase1CommandBuffer>(desc);
}

void VulkanDevice::submit(const SubmitPacket& packet)
{
    if (!impl_->is_ready())
    {
        throw std::runtime_error(std::string(impl_->initialization_status().detail));
    }

    if (!packet.command_buffers.empty())
    {
        auto& timeline_value = impl_->timelines_[queue_slot(packet.queue)];
        timeline_value += 1;
    }
}

TimelinePoint VulkanDevice::timeline(const QueueType queue) const
{
    return {queue, impl_->timelines_[queue_slot(queue)]};
}

void VulkanDevice::wait(const TimelinePoint& point)
{
    if (!impl_->is_ready())
    {
        throw std::runtime_error(std::string(impl_->initialization_status().detail));
    }

    auto& timeline_value = impl_->timelines_[queue_slot(point.queue)];
    if (timeline_value < point.value)
    {
        impl_->wait_idle();
        timeline_value = point.value;
    }
}

void VulkanDevice::wait_idle()
{
    impl_->wait_idle();
}

void VulkanDevice::begin_frame(const std::uint64_t frame_index)
{
    impl_->begin_frame(frame_index);
}

void VulkanDevice::end_frame(const std::uint64_t frame_index)
{
    impl_->end_frame(frame_index);
}

void VulkanDevice::collect_garbage()
{
    impl_->collect_garbage();
}

VulkanInitializationStatus VulkanDevice::initialization_status() const noexcept
{
    return impl_->initialization_status();
}
}
