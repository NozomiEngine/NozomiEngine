#pragma once

#include <string_view>

#include <nozomi/rhi/rhi_types.hpp>

namespace Nozomi::RHI
{
struct GlobalBarrier
{
    ResourceState before = ResourceState::k_undefined;
    ResourceState after = ResourceState::k_undefined;
};

class CommandBuffer
{
public:
    virtual ~CommandBuffer() = default;

    [[nodiscard]] virtual QueueType queue_type() const noexcept = 0;
    [[nodiscard]] virtual CommandBufferLevel level() const noexcept = 0;

    virtual void begin() = 0;
    virtual void end() = 0;
    virtual void reset() = 0;

    virtual void begin_label(std::string_view label) = 0;
    virtual void end_label() = 0;

    virtual void barrier(const GlobalBarrier& barrier) = 0;
};
}
