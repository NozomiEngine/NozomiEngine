#pragma once

#include <cstdint>
#include <string_view>

#include <nozomi/rhi/rhi_types.hpp>

namespace Nozomi::RHI
{
enum class MemoryDomain : std::uint8_t
{
    k_device_local,
    k_upload,
    k_readback
};

struct AllocationHandle
{
    std::uint64_t id = 0;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return id != 0;
    }
};

struct AllocationDesc
{
    std::string_view debug_name {};
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
    std::uint32_t memory_type_bits = 0xffffffffu;
    MemoryDomain domain = MemoryDomain::k_device_local;
    bool dedicated = false;
};

class MemoryAllocator
{
public:
    virtual ~MemoryAllocator() = default;

    [[nodiscard]] virtual MemoryAllocatorBackend backend() const noexcept = 0;
    [[nodiscard]] virtual AllocationHandle allocate(const AllocationDesc& desc) = 0;
    virtual void free(AllocationHandle allocation) = 0;

    virtual void begin_frame(std::uint64_t frame_index) = 0;
    virtual void end_frame(std::uint64_t frame_index) = 0;
    virtual void trim() = 0;

    [[nodiscard]] virtual MemoryBudget budget() const = 0;
};
}
