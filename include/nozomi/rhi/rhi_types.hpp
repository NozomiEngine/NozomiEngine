#pragma once

#include <concepts>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Nozomi::RHI
{
enum class Backend : std::uint8_t
{
    k_vulkan
};

enum class MemoryAllocatorBackend : std::uint8_t
{
    k_vma
};

enum class QueueType : std::uint8_t
{
    k_graphics,
    k_compute,
    k_transfer
};

enum class CommandBufferLevel : std::uint8_t
{
    k_primary,
    k_secondary
};

enum class Format : std::uint16_t
{
    k_undefined,
    k_b8g8r8a8_unorm,
    k_r16g16b16a16_sfloat,
    k_d32_sfloat,
    k_d32_sfloat_s8_uint
};

enum class ResourceState : std::uint32_t
{
    k_undefined = 0,
    k_transfer_src = 1u << 0u,
    k_transfer_dst = 1u << 1u,
    k_shader_read = 1u << 2u,
    k_shader_write = 1u << 3u,
    k_color_attachment = 1u << 4u,
    k_depth_stencil_attachment = 1u << 5u,
    k_present = 1u << 6u
};

template <typename EnumType>
concept bitmask_enum = std::is_enum_v<EnumType>;

template <bitmask_enum EnumType>
[[nodiscard]] constexpr auto to_underlying(EnumType value) noexcept
{
    return static_cast<std::underlying_type_t<EnumType>>(value);
}

[[nodiscard]] constexpr ResourceState operator|(ResourceState lhs, ResourceState rhs) noexcept
{
    return static_cast<ResourceState>(to_underlying(lhs) | to_underlying(rhs));
}

[[nodiscard]] constexpr ResourceState operator&(ResourceState lhs, ResourceState rhs) noexcept
{
    return static_cast<ResourceState>(to_underlying(lhs) & to_underlying(rhs));
}

constexpr ResourceState& operator|=(ResourceState& lhs, ResourceState rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

struct Extent2D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct Extent3D
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
};

struct MemoryBudget
{
    std::uint64_t bytes_used = 0;
    std::uint64_t bytes_reserved = 0;
    std::uint64_t bytes_budget = 0;
};

struct DeviceMemoryAllocatorDesc
{
    // Vulkan allocation is intentionally delegated to VMA instead of bespoke sub-allocation code.
    MemoryAllocatorBackend backend = MemoryAllocatorBackend::k_vma;
    std::uint64_t upload_arena_bytes = 256ull << 20u;
    std::uint64_t device_arena_bytes = 1024ull << 20u;
    std::uint64_t readback_arena_bytes = 128ull << 20u;
    bool enable_dedicated_allocations = true;
    bool enable_defragmentation = true;
};

struct QueueRequest
{
    QueueType type = QueueType::k_graphics;
    std::uint32_t count = 1;
    bool allow_present = false;
};

struct DeviceDesc
{
    std::span<const QueueRequest> queues {};
    DeviceMemoryAllocatorDesc allocator {};
    bool enable_validation = true;
    bool enable_gpu_markers = true;
    bool enable_pipeline_cache = true;
    bool enable_shader_debug_names = true;
};

struct CommandBufferDesc
{
    QueueType queue = QueueType::k_graphics;
    CommandBufferLevel level = CommandBufferLevel::k_primary;
    bool transient = false;
    bool resettable = true;
};

struct TimelinePoint
{
    QueueType queue = QueueType::k_graphics;
    std::uint64_t value = 0;
};
}
