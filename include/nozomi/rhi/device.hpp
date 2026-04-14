#pragma once

#include <memory>
#include <string_view>

#include <nozomi/rhi/command_buffer.hpp>
#include <nozomi/rhi/memory_allocator.hpp>
#include <nozomi/rhi/rhi_types.hpp>

namespace Nozomi::RHI
{
struct SubmitPacket
{
    QueueType queue = QueueType::k_graphics;
    std::span<CommandBuffer* const> command_buffers {};
    std::span<const TimelinePoint> wait_points {};
    std::span<const TimelinePoint> signal_points {};
};

class Device
{
public:
    virtual ~Device() = default;

    [[nodiscard]] virtual Backend backend() const noexcept = 0;
    [[nodiscard]] virtual std::string_view adapter_name() const noexcept = 0;
    [[nodiscard]] virtual const DeviceDesc& desc() const noexcept = 0;

    [[nodiscard]] virtual MemoryAllocator& memory_allocator() noexcept = 0;
    [[nodiscard]] virtual const MemoryAllocator& memory_allocator() const noexcept = 0;
    [[nodiscard]] virtual MemoryBudget memory_budget() const = 0;

    [[nodiscard]] virtual std::unique_ptr<CommandBuffer> create_command_buffer(
        const CommandBufferDesc& desc) = 0;

    virtual void submit(const SubmitPacket& packet) = 0;
    [[nodiscard]] virtual TimelinePoint timeline(QueueType queue) const = 0;
    virtual void wait(const TimelinePoint& point) = 0;
    virtual void wait_idle() = 0;

    virtual void begin_frame(std::uint64_t frame_index) = 0;
    virtual void end_frame(std::uint64_t frame_index) = 0;
    virtual void collect_garbage() = 0;
};
}
