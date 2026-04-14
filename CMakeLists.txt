cmake_minimum_required(VERSION 3.28)

project(
    nozomi_engine
    VERSION 0.1.0
    DESCRIPTION "NozomiEngine - microkernel-first 3D engine"
    LANGUAGES CXX
)

if (PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are disabled. Use a dedicated build directory.")
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(NOZOMI_BUILD_TESTS "Build test targets." ON)
option(NOZOMI_ENABLE_VULKAN "Build the Vulkan RHI backend." ON)

add_library(nozomi_third_party_vma INTERFACE)
add_library(Nozomi::ThirdParty::VMA ALIAS nozomi_third_party_vma)

set(NOZOMI_VMA_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/third_party/VulkanMemoryAllocator/include")
if (EXISTS "${NOZOMI_VMA_INCLUDE_DIR}/vk_mem_alloc.h")
    target_include_directories(nozomi_third_party_vma INTERFACE "${NOZOMI_VMA_INCLUDE_DIR}")
    target_compile_definitions(nozomi_third_party_vma INTERFACE NOZOMI_HAS_VMA=1)
else ()
    message(
        STATUS
            "VMA not found at ${NOZOMI_VMA_INCLUDE_DIR}. "
            "The Vulkan backend will not fall back to a handwritten allocator."
    )
    target_compile_definitions(nozomi_third_party_vma INTERFACE NOZOMI_HAS_VMA=0)
endif ()

function(nozomi_configure_target target_name)
    target_compile_features(${target_name} INTERFACE cxx_std_20)
    target_include_directories(
        ${target_name}
        INTERFACE
            ${PROJECT_SOURCE_DIR}/include
    )

    if (MSVC)
        target_compile_options(
            ${target_name}
            INTERFACE
                /W4
                /permissive-
                /EHsc
                /Zc:__cplusplus
                /Zc:preprocessor
                /utf-8
        )
        target_compile_definitions(
            ${target_name}
            INTERFACE
                NOMINMAX
                WIN32_LEAN_AND_MEAN
                _CRT_SECURE_NO_WARNINGS
        )
    else ()
        target_compile_options(
            ${target_name}
            INTERFACE
                -Wall
                -Wextra
                -Wpedantic
                -Wshadow
                -Wconversion
        )
    endif ()
endfunction()

add_library(nozomi_core INTERFACE)
add_library(Nozomi::Core ALIAS nozomi_core)
nozomi_configure_target(nozomi_core)
target_sources(
    nozomi_core
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/nozomi/core/build_config.hpp
)

add_library(nozomi_math INTERFACE)
add_library(Nozomi::Math ALIAS nozomi_math)
nozomi_configure_target(nozomi_math)
target_link_libraries(nozomi_math INTERFACE Nozomi::Core)

add_library(nozomi_platform INTERFACE)
add_library(Nozomi::Platform ALIAS nozomi_platform)
nozomi_configure_target(nozomi_platform)
target_link_libraries(nozomi_platform INTERFACE Nozomi::Core)

add_library(nozomi_rhi INTERFACE)
add_library(Nozomi::RHI ALIAS nozomi_rhi)
nozomi_configure_target(nozomi_rhi)
target_link_libraries(
    nozomi_rhi
    INTERFACE
        Nozomi::Core
        Nozomi::Math
        Nozomi::Platform
)
target_sources(
    nozomi_rhi
    INTERFACE
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/rhi_types.hpp
            ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/command_buffer.hpp
            ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/memory_allocator.hpp
            ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/device.hpp
            ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/vulkan/vulkan_fwd.hpp
            ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/vulkan/vulkan_device.hpp
)

if (NOZOMI_ENABLE_VULKAN)
    find_package(Vulkan QUIET)

    add_library(nozomi_rhi_vulkan STATIC)
    add_library(Nozomi::RHI::Vulkan ALIAS nozomi_rhi_vulkan)
    target_link_libraries(
        nozomi_rhi_vulkan
        PUBLIC
            Nozomi::RHI
        PRIVATE
            Nozomi::ThirdParty::VMA
    )
    target_sources(
        nozomi_rhi_vulkan
        PRIVATE
            ${PROJECT_SOURCE_DIR}/src/rhi/vulkan/vulkan_device.cpp
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES
                ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/vulkan/vulkan_fwd.hpp
                ${PROJECT_SOURCE_DIR}/include/nozomi/rhi/vulkan/vulkan_device.hpp
    )
    target_include_directories(
        nozomi_rhi_vulkan
        PUBLIC
            ${PROJECT_SOURCE_DIR}/include
    )
    target_compile_features(nozomi_rhi_vulkan PUBLIC cxx_std_20)

    if (Vulkan_FOUND)
        target_link_libraries(nozomi_rhi_vulkan PRIVATE Vulkan::Vulkan)
        target_compile_definitions(nozomi_rhi_vulkan PUBLIC NOZOMI_HAS_VULKAN_SDK=1)
    else ()
        message(STATUS "Vulkan SDK not found. Building the backend skeleton without SDK linkage.")
        target_compile_definitions(nozomi_rhi_vulkan PUBLIC NOZOMI_HAS_VULKAN_SDK=0)
    endif ()

    set_target_properties(nozomi_rhi_vulkan PROPERTIES FOLDER "Runtime/RHI")
endif ()

if (NOZOMI_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif ()
