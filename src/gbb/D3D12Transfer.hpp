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

#include <d3d12.h>
#include <wrl/client.h>

namespace gbb {
class D3D12Transfer : public Transfer {
public:
  D3D12Transfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx);
  ~D3D12Transfer();

  std::string getApiName() const override { return "D3D12"; }
  Result execute(uint32_t p_seed) override;

private:
  struct CommandRecordingContext {
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
  };

  uint32_t m_fallbackDevIdx;
  CommandRecordingContext m_srcCmdRec;
  CommandRecordingContext m_dstCmdRec;
  Microsoft::WRL::ComPtr<ID3D12Resource2> m_srcBuffer;
  Microsoft::WRL::ComPtr<ID3D12Resource2> m_dstBuffer;
  Microsoft::WRL::ComPtr<ID3D12Resource2> m_errorCountBuffer;
  uint32_t *m_errorCountMappedPtr = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_timestampBuffer;
  uint64_t *m_timestampMappedPtr = nullptr;
  Microsoft::WRL::ComPtr<ID3D12Fence1> m_fence;
  uint64_t m_fenceValue = 0;
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_queryHeap;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_genPipelineState;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_checkPipelineState;

  CommandRecordingContext createCommandRecordingContext(uint32_t p_devIdx) const;
  Microsoft::WRL::ComPtr<ID3D12Resource2> createBufferResource(DeviceIndex p_devIdx, size_t p_byteSize) const;
  void recordDataGeneration(uint32_t p_seed) const;
  void recordDataCopy() const;
  void recordDataCheck(uint32_t p_seed) const;
  Result readbackResult() const;
  uint32_t getErrorCount() const;
};
} // namespace gbb