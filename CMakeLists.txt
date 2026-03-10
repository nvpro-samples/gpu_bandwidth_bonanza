# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES.
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

project(gpu_bandwidth_bonanza VERSION 1.0.5)

# Enable only the nvpro_core2 libraries we use
set(NVPRO2_ENABLE_DEFAULT OFF)

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
    src/gbb/Api.hpp
    src/gbb/DeviceIndex.hpp
    src/gbb/Logger.cpp
    src/gbb/Logger.hpp
    src/gbb/Options.cpp
    src/gbb/Options.hpp
    src/gbb/ResultProcessor.cpp
    src/gbb/ResultProcessor.hpp
    src/gbb/SystemMemoryTransfer.cpp
    src/gbb/SystemMemoryTransfer.hpp
    src/gbb/Transfer.cpp
    src/gbb/Transfer.hpp
    src/gbb/TransferBenchmark.cpp
    src/gbb/TransferBenchmark.hpp
    src/gbb/UuidUtils.hpp
    src/gbb/VulkanApi.cpp
    src/gbb/VulkanApi.hpp
    src/gbb/VulkanTransfer.cpp
    src/gbb/VulkanTransfer.hpp
    src/main.cpp
)

target_include_directories(
    gpu_bandwidth_bonanza
    PUBLIC src
)

list(
    APPEND GBB_CUDA_SOURCES
    src/gbb/CudaApi.cpp
    src/gbb/CudaApi.hpp
    src/gbb/CudaTransfer.cu
    src/gbb/CudaTransfer.hpp
    src/gbb/NvmlInterface.cpp
    src/gbb/NvmlInterface.hpp
)

list(
    APPEND GBB_D3D12_SOURCES
    src/gbb/D3D12Api.cpp
    src/gbb/D3D12Api.hpp
    src/gbb/D3D12Transfer.cpp
    src/gbb/D3D12Transfer.hpp
)

list(
    APPEND GBB_SHADER_SOURCE_LISTS
    shaders/gbb.slang,gen,check
)

target_link_libraries(
    gpu_bandwidth_bonanza
    PUBLIC Vulkan::Vulkan
    PUBLIC fmt
)

if(MSVC)
    target_compile_definitions(gpu_bandwidth_bonanza PUBLIC $<$<COMPILE_LANGUAGE:CUDA>:FMT_UNICODE=0>)
endif()

# ###############################################################################
# Vulkan/D3D12 shaders
#
find_package(Slang REQUIRED)

if(WIN32)
    # Since Slang requires dxcompiler.dll to emit DXIL, but doesn't include it
    # in its SDK and only looks for it locally, we need to copy it into the
    # SDK directory.

    # find 'dxcompiler.dll', which lies in the bin directory next to dxc.exe
    get_filename_component(DXC_LIBRARY_PATH ${Vulkan_dxc_EXECUTABLE} DIRECTORY)

    file(COPY_FILE ${DXC_LIBRARY_PATH}/dxcompiler.dll ${Slang_ROOT}/bin/dxcompiler.dll ONLY_IF_DIFFERENT)
endif()

foreach(SHADER_SOURCE_OBJECT ${GBB_SHADER_SOURCE_LISTS})
    # Each entry in GBB_SHADER_SOURCES is a comma-separated list of a source file,
	# followed by its entrypoints.
    string(REPLACE "," ";" TMP_SHADER_SOURCE_LIST ${SHADER_SOURCE_OBJECT})
    list(POP_FRONT TMP_SHADER_SOURCE_LIST TMP_SHADER_SOURCE)
	set(TMP_SHADER_SOURCE ${CMAKE_CURRENT_LIST_DIR}/${TMP_SHADER_SOURCE})
    list(APPEND GBB_SHADER_SOURCES ${TMP_SHADER_SOURCE})

    compile_slang(
        "${TMP_SHADER_SOURCE}"                # Input
        "${CMAKE_BINARY_DIR}/include/shaders" # Output directory
        TARGET      "spirv"                   #
        NAME_SUFFIX ".spv"                    # Added to the output file + variable name
        EXTRA_FLAGS -source-embed-style u32   # Extra slangc command-line flags
        HEADERS_VAR _INL_FILE                 # _INL_FILE will be set to the output file
    )
    list(APPEND GBB_SHADERS_INL_FILES ${_INL_FILE})
    
    if(WIN32)
	    foreach(TMP_ENTRY ${TMP_SHADER_SOURCE_LIST})
			compile_slang(
				"${TMP_SHADER_SOURCE}"
				"${CMAKE_BINARY_DIR}/include/shaders"
				TARGET      "dxil"
				NAME_SUFFIX ".${TMP_ENTRY}.dxil"
				PROFILE     sm_6_0
				EXTRA_FLAGS -source-embed-style u32 -entry ${TMP_ENTRY}
				HEADERS_VAR _INL_FILE
			)
			list(APPEND GBB_SHADERS_INL_FILES ${_INL_FILE})
		endforeach()
    endif()
endforeach()

# Add sources and target files to the source list so they're added as dependencies
target_sources(gpu_bandwidth_bonanza PRIVATE ${GBB_SHADER_SOURCES} ${GBB_SHADERS_INL_FILES})
source_group("Compiled Shaders" FILES ${GBB_SHADERS_INL_FILES})

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
    message(STATUS "Building with CUDA and NVML support: YES")
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
    message(STATUS "Building with CUDA and NVML support: NO")
endif()

# ###############################################################################
# Set 'gpu_bandwidth_bonanza' as the startup project for the generated Visual Studio solution
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT gpu_bandwidth_bonanza)

# ###############################################################################
# Copy shared libraries this sample links with to the output and install directories
copy_to_runtime_and_install(gpu_bandwidth_bonanza AUTO)
