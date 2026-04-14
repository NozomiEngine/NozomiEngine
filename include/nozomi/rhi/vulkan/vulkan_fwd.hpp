#pragma once

#include <cstdint>

#if __has_include(<vulkan/vulkan_core.h>)
#include <vulkan/vulkan_core.h>
#else
extern "C"
{
struct VkInstance_T;
struct VkPhysicalDevice_T;
struct VkDevice_T;
struct VkQueue_T;
struct VkCommandPool_T;
struct VkCommandBuffer_T;
struct VkAllocationCallbacks;

using VkInstance = VkInstance_T*;
using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkDevice = VkDevice_T*;
using VkQueue = VkQueue_T*;
using VkCommandPool = VkCommandPool_T*;
using VkCommandBuffer = VkCommandBuffer_T*;
using VkDeviceSize = std::uint64_t;
}
#endif
