# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(gpu_bandwidth_bonanza VERSION 1.0.4)

# Turn off nvpro_core2 libraries we don't use
option(NVPRO2_ENABLE_nvaftermath off)
option(NVPRO2_ENABLE_nvapp off)
option(NVPRO2_ENABLE_nvgl off)
option(NVPRO2_ENABLE_nvgui off)
option(NVPRO2_ENABLE_nvgpu_monitor off)
option(NVPRO2_ENABLE_nvimageformats off)
option(NVPRO2_ENABLE_nvshaders_host off)
option(NVPRO2_ENABLE_nvvkglsl off)
option(NVPRO2_ENABLE_nvvkgltf off)
option(NVPRO2_ENABLE_nvutils off)
option(NVPRO2_ENABLE_nvvk off)
option(NVPRO2_ENABLE_nvnsight off)

# Add the cmake folder to the module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
find_package(NvproCore2 REQUIRED)

find_package(Vulkan REQUIRED COMPONENTS dxc)

find_package(Git)

if(${Git_FOUND})
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short HEAD
        OUTPUT_VARIABLE APP_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_SOURCE_DIR} diff --quiet
        RESULT_VARIABLE APP_HAS_UNCOMMITTED_CHANGES
    )
    add_compile_definitions(
        APP_COMMIT_HASH="${APP_COMMIT_HASH}"
        APP_HAS_UNCOMMITTED_CHANGES=${APP_HAS_UNCOMMITTED_CHANGES}
    )
endif()

add_compile_definitions(
    APP_NAME="${CMAKE_PROJECT_NAME}"
    APP_VERSION_MAJOR="${PROJECT_VERSION_MAJOR}"
    APP_VERSION_MINOR="${PROJECT_VERSION_MINOR}"
    APP_VERSION_PATCH="${PROJECT_VERSION_PATCH}"
)

add_executable(
    gpu_bandwidth_bonanza
    src/gbb/Logger.cpp
    src/gbb/Options.cpp
    src/gbb/ResultProcessor.cpp
    src/gbb/SystemMemoryTransfer.cpp
    src/gbb/Transfer.cpp
    src/gbb/TransferBenchmark.cpp
    src/gbb/VulkanApi.cpp
    src/gbb/VulkanTransfer.cpp
    src/main.cpp
)

target_include_directories(
    gpu_bandwidth_bonanza
    PUBLIC src
)

list(
    APPEND GBB_CUDA_SOURCES
    src/gbb/CudaApi.cpp
    src/gbb/CudaTransfer.cu
    src/gbb/NvmlInterface.cpp
)

list(
    APPEND GBB_D3D12_SOURCES
    src/gbb/D3D12Api.cpp
    src/gbb/D3D12Transfer.cpp
)

list(
    APPEND GBB_SHADER_SOURCES
    shaders/gbb.slang,gen,check
)

target_link_libraries(
    gpu_bandwidth_bonanza
    PUBLIC Vulkan::Vulkan
    PUBLIC fmt
)

if(MSVC)
    target_compile_definitions(gpu_bandwidth_bonanza PUBLIC "FMT_UNICODE=0")
endif()

# ###############################################################################
# Vulkan/D3D12 shaders
#
find_package(Slang REQUIRED)

if(WIN32)
    # On Windows, we need to find dxcompiler.dll (coming as part of the Vulkan SDK)
    # and place it next to slangc (our downloaded executable). Otherwise it
    # will complain about not being able to load dxc.
    # This is likely due to a bug in how slangc is opening dxcompiler.dll.

    # Get the directory containing the slangc executable
    get_filename_component(SLANGC_DIRECTORY ${Slang_SLANGC_EXECUTABLE} DIRECTORY)

    # find 'dxcompiler.dll', which lies in the bin directory next to dxc.exe
    get_filename_component(DXC_LIBRARY_PATH ${Vulkan_dxc_EXECUTABLE} DIRECTORY)

    # Vulkan_dxc_LIBRARY points to the lib/dxcompiler.lib file
    get_filename_component(DXC_LIBRARY_NAME ${Vulkan_dxc_LIBRARY} NAME_WLE)
    set(DXC_LIBRARY_DLL ${DXC_LIBRARY_PATH}/${DXC_LIBRARY_NAME}.dll)
    set(DXC_LIBRARY_COPY_PATH ${SLANGC_DIRECTORY}/${DXC_LIBRARY_NAME}.dll)

    add_custom_command(
        OUTPUT ${DXC_LIBRARY_COPY_PATH}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DXC_LIBRARY_DLL} ${DXC_LIBRARY_COPY_PATH}
        DEPENDS ${DXC_LIBRARY_DLL}
        COMMENT "Copying DXC library to slangc directory: ${DXC_LIBRARY_COPY_PATH}"
    )
    add_custom_target(copy_dxc_library ALL DEPENDS ${DXC_LIBRARY_COPY_PATH})
endif()

function(gbb_slang_to_inl source_file source_entry inl_file slangc_target)
    get_filename_component(TMP_INL_DIRECTORY ${inl_file} DIRECTORY)
    add_custom_command(
        OUTPUT ${inl_file}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${TMP_INL_DIRECTORY}
        COMMAND ${Slang_SLANGC_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/${source_file} -matrix-layout-row-major -target ${slangc_target} -profile sm_6_0 -entry ${source_entry} -o ${inl_file} -source-embed-style u32 -source-embed-name ${source_entry}_${slangc_target}
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/${source_file}
        COMMENT "Compiling source: ${source_file}, entry: ${source_entry}, target: ${slangc_target}"
    )
endfunction()

foreach(SHADER_SOURCE_OBJECT ${GBB_SHADER_SOURCES})
    string(REPLACE "," ";" TMP_SHADER_SOURCE_LIST ${SHADER_SOURCE_OBJECT})
    list(POP_FRONT TMP_SHADER_SOURCE_LIST TMP_SHADER_SOURCE)

    foreach(TMP_ENTRY ${TMP_SHADER_SOURCE_LIST})
        set(SPIRV_SHADER_INL_FILE ${CMAKE_BINARY_DIR}/include/${TMP_SHADER_SOURCE}.${TMP_ENTRY}.spv.h)
        gbb_slang_to_inl(${TMP_SHADER_SOURCE} ${TMP_ENTRY} ${SPIRV_SHADER_INL_FILE} "spirv")
        list(APPEND GBB_SHADERS_INL_FILES ${SPIRV_SHADER_INL_FILE})

        if(WIN32)
            set(DXIL_SHADER_INL_FILE ${CMAKE_BINARY_DIR}/include/${TMP_SHADER_SOURCE}.${TMP_ENTRY}.dxil.h)
            gbb_slang_to_inl(${TMP_SHADER_SOURCE} ${TMP_ENTRY} ${DXIL_SHADER_INL_FILE} "dxil")
            list(APPEND GBB_SHADERS_INL_FILES ${DXIL_SHADER_INL_FILE})
        endif()
    endforeach()
endforeach()

add_custom_target(
    gpu_bandwidth_bonanza_shaders
    DEPENDS ${GBB_SHADERS_INL_FILES}
)

if(WIN32)
    add_dependencies(gpu_bandwidth_bonanza_shaders copy_dxc_library)
endif()

add_dependencies(
    gpu_bandwidth_bonanza
    gpu_bandwidth_bonanza_shaders
)

target_include_directories(
    gpu_bandwidth_bonanza
    PRIVATE ${CMAKE_BINARY_DIR}/include
)

# ###############################################################################
# optional: D3D12
#
if(WIN32)
    add_compile_definitions(GBB_D3D12_ENABLED)
    target_sources(gpu_bandwidth_bonanza PRIVATE ${GBB_D3D12_SOURCES})
    target_link_libraries(
        gpu_bandwidth_bonanza
        PRIVATE dxgi
        PRIVATE d3d12
    )
endif()

# ###############################################################################
# optional: CUDA lib
#
find_package(CUDAToolkit)
set(NVIDIA_PROPRIETARY ${CUDAToolkit_FOUND} CACHE BOOL "Build with CUDA and NVML support.")

if(${NVIDIA_PROPRIETARY})
    message("Building with CUDA and NVML support: YES")
    add_compile_definitions(GBB_CUDA_ENABLED)
    find_package(CUDAToolkit REQUIRED COMPONENTS CUDA::nvml)
    enable_language(CUDA)
    target_sources(gpu_bandwidth_bonanza PRIVATE ${GBB_CUDA_SOURCES})
    target_link_libraries(
        gpu_bandwidth_bonanza
        PRIVATE CUDA::cuda_driver
        PRIVATE CUDA::nvml
    )

    # This is a hack we're setting it to OFF as opposed to 'all' or 'all-major'
    # because these are only supported from CMake 3.23 onwards. 'OFF' will silently fail
    # but still make CUDA choose appropriate settings.
    set_property(TARGET gpu_bandwidth_bonanza PROPERTY CUDA_ARCHITECTURES OFF)
else()
    message("Building with CUDA and NVML support: NO")
endif()

# ###############################################################################
# Set 'gpu_bandwidth_bonanza' as the startup project for the generated Visual Studio solution
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT gpu_bandwidth_bonanza)
