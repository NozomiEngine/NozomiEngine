#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

#include <nozomi/rhi/device.hpp>
#include <nozomi/rhi/vulkan/vulkan_fwd.hpp>

namespace Nozomi::RHI
{
struct VulkanDeviceDesc
{
    DeviceDesc base {};
    void* native_window = nullptr;
    std::span<const char* const> instance_layers {};
    std::span<const char* const> instance_extensions {};
    std::span<const char* const> device_extensions {};
    const VkAllocationCallbacks* allocation_callbacks = nullptr;
    std::uint32_t frames_in_flight = 3;
    bool enable_dynamic_rendering = true;
    bool enable_descriptor_buffer = false;
    bool enable_shader_object = false;
};

enum class VulkanInitializationState : std::uint8_t
{
    k_uninitialized,
    k_ready,
    k_failed
};

struct VulkanInitializationStatus
{
    VulkanInitializationState state = VulkanInitializationState::k_uninitialized;
    std::string_view detail {};

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return state == VulkanInitializationState::k_ready;
    }
};

class VulkanDevice final : public Device
{
public:
    explicit VulkanDevice(const VulkanDeviceDesc& desc);
    ~VulkanDevice() override;

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) noexcept = delete;
    VulkanDevice& operator=(VulkanDevice&&) noexcept = delete;

    [[nodiscard]] Backend backend() const noexcept override;
    [[nodiscard]] std::string_view adapter_name() const noexcept override;
    [[nodiscard]] const DeviceDesc& desc() const noexcept override;

    [[nodiscard]] MemoryAllocator& memory_allocator() noexcept override;
    [[nodiscard]] const MemoryAllocator& memory_allocator() const noexcept override;
    [[nodiscard]] MemoryBudget memory_budget() const override;

    [[nodiscard]] std::unique_ptr<CommandBuffer> create_command_buffer(
        const CommandBufferDesc& desc) override;

    void submit(const SubmitPacket& packet) override;
    [[nodiscard]] TimelinePoint timeline(QueueType queue) const override;
    void wait(const TimelinePoint& point) override;
    void wait_idle() override;

    void begin_frame(std::uint64_t frame_index) override;
    void end_frame(std::uint64_t frame_index) override;
    void collect_garbage() override;

    // Bootstrap details stay inside the Pimpl. Callers only learn whether startup succeeded.
    [[nodiscard]] VulkanInitializationStatus initialization_status() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
