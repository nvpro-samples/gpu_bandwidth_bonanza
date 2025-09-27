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
#pragma once
#include "Api.hpp"

#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cuda.h>
#include <cuda_runtime.h>
#include <vulkan/vulkan_core.h>

#define GBB_WARN_UNLESS_CUDA(_cmd)                                                                                     \
  do {                                                                                                                 \
    auto _res = _cmd;                                                                                                  \
    GBB_WARN_UNLESS(gbb::CudaApi::isSuccess(_res), "{}", gbb::CudaApi::resultToString(_res));                          \
  } while (0)

#define GBB_THROW_UNLESS_CUDA(_cmd)                                                                                    \
  do {                                                                                                                 \
    auto _res = _cmd;                                                                                                  \
    GBB_THROW_UNLESS(gbb::CudaApi::isSuccess(_res), "{}", gbb::CudaApi::resultToString(_res));                         \
  } while (0)

namespace gbb {
class CudaApi : public Api {
public:
  static bool isSuccess(CUresult p_result) { return p_result == CUresult::CUDA_SUCCESS; }
  static bool isSuccess(cudaError p_result) { return p_result == cudaError::cudaSuccess; }
  static std::string resultToString(CUresult p_result);
  static std::string resultToString(cudaError p_result);

  CudaApi();
  ~CudaApi();

  std::string getName() override { return "CUDA"; }
  uint32_t getPhysicalDeviceCount() override { return static_cast<uint32_t>(m_devices.size()); }
  std::unique_ptr<Transfer> createTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx,
                                           DeviceIndex p_dstDevIdx) override;

  CUdevice getDevice(uint32_t p_devIdx) const { return m_devices[p_devIdx].value(); }
  CUcontext getContext(uint32_t p_devIdx) const { return m_contexts[p_devIdx]; }
  CUuuid getUuid(uint32_t p_devIdx) const;

private:
  std::vector<std::optional<CUdevice>> m_devices;
  std::vector<CUcontext> m_contexts;

  std::optional<uint32_t> getDeviceIndexIfSelected(uint32_t p_deviceOrdinal) const;
};

inline std::unique_ptr<CudaApi> g_cuda;
} // namespace gbb
