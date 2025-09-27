/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "CudaApi.hpp"

#include "CudaTransfer.hpp"
#include "Logger.hpp"
#include "UuidUtils.hpp"
#include "VulkanApi.hpp"

#ifdef GBB_D3D12_ENABLED
#include "D3D12Api.hpp"
#endif

#include <cstdint>
#include <cuda.h>
#include <driver_types.h>

namespace gbb {
CudaApi::CudaApi() {
  GBB_THROW_UNLESS_CUDA(cuInit(0));
  int32_t deviceCount;
  GBB_THROW_UNLESS_CUDA(cuDeviceGetCount(&deviceCount));
  GBB_THROW_UNLESS(deviceCount != 0, "No CUDA devices.");
  for (int32_t i = 0; i < deviceCount; ++i) {
    CUdevice device = {};
    GBB_THROW_UNLESS_CUDA(cuDeviceGet(&device, i));
    char name[128] = {};
    GBB_THROW_UNLESS_CUDA(cuDeviceGetName(name, sizeof(name), device));
    CUuuid uuid = {};
    GBB_THROW_UNLESS_CUDA(cuDeviceGetUuid(&uuid, device));

    char luid[8] = {};
    uint32_t deviceNodeMask = 0;
    std::string luidInfo;
    if (cuDeviceGetLuid(luid, &deviceNodeMask, device) == CUresult::CUDA_SUCCESS) {
      luidInfo = fmt::format(", luid [{}]]", luidToString(luid));
    }

    std::optional<uint32_t> deviceIndex = this->getDeviceIndexIfSelected(device);
    if (deviceIndex) {
      m_devices.resize(std::max(m_devices.size(), static_cast<size_t>(deviceIndex.value() + 1)));
      m_devices[deviceIndex.value()] = device;
    }

    GBB_INFO("CUDA device {}: {}, uuid [{}]{}{}", i, name, uuidToString(uuid.bytes), luidInfo,
             deviceIndex ? GBB_FORMAT_CYAN(", selected (index {})", deviceIndex.value()) : "");
  }
  GBB_THROW_IF(m_devices.empty(), "No or no matching CUDA devices found.");
  for (size_t i = 0; i < m_devices.size(); ++i) {
    GBB_THROW_UNLESS(m_devices[i], "There is no CUDA equivalent for physical device {}.", i);
  }
  m_contexts.resize(m_devices.size());
  for (size_t i = 0; i < m_contexts.size(); ++i) {
    GBB_THROW_UNLESS_CUDA(cuDevicePrimaryCtxRetain(&m_contexts[i], m_devices[i].value()));
    GBB_THROW_UNLESS_CUDA(cuCtxPushCurrent(m_contexts[i]));
    for (size_t j = 0; j < i; ++j) {
      GBB_WARN_UNLESS_CUDA(cuCtxEnablePeerAccess(m_contexts[j], 0));
    }
    GBB_THROW_UNLESS_CUDA(cuCtxPopCurrent(nullptr));
  }
}

CudaApi::~CudaApi() {
  for (auto &dev : m_devices) {
    cuDevicePrimaryCtxRelease(dev.value());
  }
}

std::unique_ptr<Transfer> CudaApi::createTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx) {
  return std::make_unique<CudaTransfer>(p_byteSize, p_srcDevIdx, p_dstDevIdx);
}

std::string CudaApi::resultToString(CUresult p_result) {
  const char *errorString = nullptr;
  if (CUresult r = cuGetErrorString(p_result, &errorString); r != CUresult::CUDA_SUCCESS) {
    GBB_ERROR("cuGetErrorString failed with {}", static_cast<uint32_t>(r));
    return std::to_string(p_result);
  }
  return errorString;
}

std::optional<uint32_t> CudaApi::getDeviceIndexIfSelected(uint32_t p_deviceOrdinal) const {
  CUdevice device = {};
  GBB_THROW_UNLESS_CUDA(cuDeviceGet(&device, p_deviceOrdinal));

  if (g_vulkan) {
    CUuuid uuid = {};
    GBB_THROW_UNLESS_CUDA(cuDeviceGetUuid(&uuid, device));
    for (uint32_t i = 0; i < g_vulkan->getPhysicalDeviceCount(); ++i) {
      if (memcmp(&uuid, g_vulkan->getUuid(i), sizeof(CUuuid)) == 0) {
        return i;
      }
    }
    return std::nullopt;
  }

#ifdef GBB_D3D12_ENABLED
  if (g_d3d12) {
    char luid[8] = {};
    uint32_t deviceNodeMask = 0;
    if (cuDeviceGetLuid(luid, &deviceNodeMask, device) == CUresult::CUDA_SUCCESS &&
        memcmp(luid, &g_d3d12->getLuid(), sizeof(luid)) == 0) {
      uint32_t deviceIndex = 0;
      while (deviceNodeMask != 1) {
        ++deviceIndex;
        deviceNodeMask >>= 1;
      }
      return deviceIndex;
    }
    return std::nullopt;
  }
#endif

  return p_deviceOrdinal;
}

CUuuid CudaApi::getUuid(uint32_t p_devIdx) const {
  CUuuid uuid = {};
  GBB_THROW_UNLESS_CUDA(cuDeviceGetUuid(&uuid, m_devices[p_devIdx].value()));
  return uuid;
}

std::string CudaApi::resultToString(cudaError p_result) { return cudaGetErrorName(p_result); }
} // namespace gbb
