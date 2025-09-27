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
#include "Transfer.hpp"

#include <cuda.h>

namespace gbb {
class CudaTransfer : public Transfer {
public:
  CudaTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx);
  ~CudaTransfer();

  std::string getApiName() const override { return "CUDA"; }
  Result execute(uint32_t p_seed) override;

private:
  struct CudaMemAllocation {
    CUdeviceptr devPtr = {};
    void *hostPtr = nullptr;

    void allocHost(size_t p_byteSize);
    void alloc(size_t p_byteSize);
    void free();
  };

  uint32_t m_bufferSize;
  CUcontext m_srcCtx;
  CUcontext m_dstCtx;
  CudaMemAllocation m_srcMem;
  CudaMemAllocation m_dstMem;
  CUevent m_evtA;
  CUevent m_evtB;
  CudaMemAllocation m_errorCountMem;
};
} // namespace gbb
