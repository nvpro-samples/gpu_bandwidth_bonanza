/*
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES.
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
#include "D3D12Transfer.hpp"

#include "D3D12Api.hpp"
#include "Logger.hpp"

#include <vector>

#include "shaders/gbb.slang.check.dxil.h"
#include "shaders/gbb.slang.gen.dxil.h"

using Microsoft::WRL::ComPtr;

namespace gbb {
struct ShaderInput {
  uint32_t seed;
  uint32_t dataBufferSize;
};

D3D12Transfer::D3D12Transfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx)
    : Transfer(p_byteSize, p_srcDevIdx, p_dstDevIdx),
      m_fallbackDevIdx(p_srcDevIdx.getOrIfHost(p_dstDevIdx.getOrIfHost(0))) {
  GBB_THROW_UNLESS(p_byteSize % sizeof(uint32_t) == 0,
                   "Given byte size ({}) must be a multiple of sizeof(uint32_t) ({}).", p_byteSize, sizeof(uint32_t));
  m_srcCmdRec = this->createCommandRecordingContext(p_srcDevIdx.getOrIfHost(m_fallbackDevIdx));
  m_dstCmdRec = this->createCommandRecordingContext(p_dstDevIdx.getOrIfHost(m_fallbackDevIdx));
  m_srcBuffer = this->createBufferResource(p_srcDevIdx, this->getByteSize());
  m_dstBuffer = this->createBufferResource(p_dstDevIdx, this->getByteSize());
  m_errorCountBuffer = this->createBufferResource(DeviceIndex::HOST, sizeof(uint32_t));
  D3D12_RANGE errorCountBufferRange = {.Begin = 0, .End = sizeof(uint32_t)};
  GBB_THROW_UNLESS_D3D(
      m_errorCountBuffer->Map(0, &errorCountBufferRange, reinterpret_cast<void **>(&m_errorCountMappedPtr)));
  m_timestampBuffer = this->createBufferResource(DeviceIndex::HOST, 2 * sizeof(uint64_t));
  D3D12_RANGE timestampBufferRange = {.Begin = 0, .End = 2 * sizeof(uint64_t)};
  GBB_THROW_UNLESS_D3D(
      m_timestampBuffer->Map(0, &timestampBufferRange, reinterpret_cast<void **>(&m_timestampMappedPtr)));

  GBB_THROW_UNLESS_D3D(g_d3d12->dev()->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&m_fence)));
  D3D12_QUERY_HEAP_DESC queryHeapDesc = {
      .Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP, .Count = 2, .NodeMask = 1u << p_srcDevIdx.getOrIfHost(m_fallbackDevIdx)};
  GBB_THROW_UNLESS_D3D(g_d3d12->dev()->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap)));

  D3D12_ROOT_PARAMETER rootParams[] = {
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
       .Constants = {.ShaderRegister = 0, .RegisterSpace = 0, .Num32BitValues = 2},
       .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 0, .RegisterSpace = 0}},
      {.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV, .Descriptor = {.ShaderRegister = 1, .RegisterSpace = 0}}};
  D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {
      .NumParameters = static_cast<UINT>(std::size(rootParams)),
      .pParameters = rootParams,
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE,
  };
  ComPtr<ID3DBlob> rootSig;
  ComPtr<ID3DBlob> rootSigError;
  GBB_THROW_UNLESS(
      SUCCEEDED(D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, rootSig.GetAddressOf(),
                                            rootSigError.GetAddressOf())),
      "{}",
      std::string(reinterpret_cast<const char *>(rootSigError->GetBufferPointer()),
                  reinterpret_cast<const char *>(rootSigError->GetBufferPointer()) + rootSigError->GetBufferSize()));
  GBB_THROW_UNLESS_D3D(g_d3d12->dev()->CreateRootSignature(g_d3d12->getNodeMaskAll(), rootSig->GetBufferPointer(),
                                                           rootSig->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
  D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineStateDesc = {
      .pRootSignature = m_rootSignature.Get(),
      .CS = {.pShaderBytecode = gbb_slang_gen_dxil, .BytecodeLength = gbb_slang_gen_dxil_sizeInBytes},
      .NodeMask = g_d3d12->getNodeMaskAll(),
      .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
  GBB_THROW_UNLESS_D3D(
      g_d3d12->dev()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_genPipelineState)));
  pipelineStateDesc.CS = {.pShaderBytecode = gbb_slang_check_dxil, .BytecodeLength = gbb_slang_check_dxil_sizeInBytes};
  GBB_THROW_UNLESS_D3D(
      g_d3d12->dev()->CreateComputePipelineState(&pipelineStateDesc, IID_PPV_ARGS(&m_checkPipelineState)));
}

D3D12Transfer::~D3D12Transfer() {
  m_timestampBuffer->Unmap(0, nullptr);
  m_errorCountBuffer->Unmap(0, nullptr);
}

D3D12Transfer::CommandRecordingContext D3D12Transfer::createCommandRecordingContext(uint32_t p_devIdx) const {
  CommandRecordingContext cmdRec;
  D3D12_COMMAND_QUEUE_DESC cmdQueueDesc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           .Priority = 0,
                                           .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
                                           .NodeMask = 1u << p_devIdx};
  GBB_THROW_UNLESS_D3D(g_d3d12->dev()->CreateCommandQueue(&cmdQueueDesc, IID_PPV_ARGS(&cmdRec.queue)));
  GBB_THROW_UNLESS_D3D(g_d3d12->dev()->CreateCommandList1(1u << p_devIdx, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&cmdRec.list)));
  GBB_THROW_UNLESS_D3D(
      g_d3d12->dev()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cmdRec.allocator)));
  return cmdRec;
}

ComPtr<ID3D12Resource2> D3D12Transfer::createBufferResource(DeviceIndex p_devIdx, size_t p_byteSize) const {
  D3D12_HEAP_PROPERTIES heapProps = {.CreationNodeMask = 1u << p_devIdx.getOrIfHost(m_fallbackDevIdx),
                                     .VisibleNodeMask = g_d3d12->getNodeMaskAll()};
  if (p_devIdx.isHost()) {
    heapProps.Type = D3D12_HEAP_TYPE_CUSTOM;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
  } else {
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
  }
  D3D12_RESOURCE_DESC resDesc = {.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                 .Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT,
                                 .Width = p_byteSize,
                                 .Height = 1,
                                 .DepthOrArraySize = 1,
                                 .MipLevels = 1,
                                 .Format = DXGI_FORMAT_UNKNOWN,
                                 .SampleDesc = {.Count = 1, .Quality = 0},
                                 .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                 .Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS};
  ComPtr<ID3D12Resource2> resource;
  GBB_THROW_UNLESS_D3D(g_d3d12->dev()->CreateCommittedResource(
      &heapProps, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource)));
  return resource;
}

Transfer::Result D3D12Transfer::execute(uint32_t p_seed) {
  GBB_THROW_UNLESS_D3D(m_srcCmdRec.allocator->Reset());
  GBB_THROW_UNLESS_D3D(m_srcCmdRec.list->Reset(m_srcCmdRec.allocator.Get(), nullptr));
  GBB_THROW_UNLESS_D3D(m_dstCmdRec.allocator->Reset());
  GBB_THROW_UNLESS_D3D(m_dstCmdRec.list->Reset(m_dstCmdRec.allocator.Get(), nullptr));

  this->recordDataGeneration(p_seed);

  D3D12_RESOURCE_BARRIER srcBufferUavBarrier = {.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV,
                                                .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
                                                .UAV = {.pResource = m_srcBuffer.Get()}};
  m_srcCmdRec.list->ResourceBarrier(1, &srcBufferUavBarrier);

  this->recordDataCopy();

  GBB_THROW_UNLESS_D3D(m_srcCmdRec.list->Close());
  ID3D12CommandList *srcCmdLists[] = {m_srcCmdRec.list.Get()};
  m_srcCmdRec.queue->ExecuteCommandLists(std::size(srcCmdLists), srcCmdLists);
  GBB_THROW_UNLESS_D3D(m_srcCmdRec.queue->Signal(m_fence.Get(), ++m_fenceValue));

  this->recordDataCheck(p_seed);

  GBB_THROW_UNLESS_D3D(m_dstCmdRec.list->Close());
  GBB_THROW_UNLESS_D3D(m_dstCmdRec.queue->Wait(m_fence.Get(), m_fenceValue));
  ID3D12CommandList *dstCmdLists[] = {m_dstCmdRec.list.Get()};
  m_dstCmdRec.queue->ExecuteCommandLists(std::size(dstCmdLists), dstCmdLists);
  GBB_THROW_UNLESS_D3D(m_dstCmdRec.queue->Signal(m_fence.Get(), ++m_fenceValue));
  GBB_THROW_UNLESS_D3D(m_fence->SetEventOnCompletion(m_fenceValue, NULL));

  return this->readbackResult();
}

void D3D12Transfer::recordDataGeneration(uint32_t p_seed) const {
  m_srcCmdRec.list->SetComputeRootSignature(m_rootSignature.Get());
  m_srcCmdRec.list->SetPipelineState(m_genPipelineState.Get());
  ShaderInput shaderInput = {.seed = p_seed,
                             .dataBufferSize = static_cast<uint32_t>(this->getByteSize() / sizeof(uint32_t))};
  m_srcCmdRec.list->SetComputeRoot32BitConstants(0, 2, &shaderInput, 0);
  m_srcCmdRec.list->SetComputeRootUnorderedAccessView(1, m_srcBuffer->GetGPUVirtualAddress());
  m_srcCmdRec.list->Dispatch((shaderInput.dataBufferSize + 1023) / 1024, 1, 1);
}

void D3D12Transfer::recordDataCopy() const {
  m_srcCmdRec.list->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
  m_srcCmdRec.list->CopyBufferRegion(m_dstBuffer.Get(), 0, m_srcBuffer.Get(), 0, this->getByteSize());
  m_srcCmdRec.list->EndQuery(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
  m_srcCmdRec.list->ResolveQueryData(m_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, m_timestampBuffer.Get(), 0);
}

void D3D12Transfer::recordDataCheck(uint32_t p_seed) const {
  m_errorCountMappedPtr[0] = 0;
  m_dstCmdRec.list->SetComputeRootSignature(m_rootSignature.Get());
  m_dstCmdRec.list->SetPipelineState(m_checkPipelineState.Get());
  ShaderInput shaderInput = {.seed = p_seed,
                             .dataBufferSize = static_cast<uint32_t>(this->getByteSize() / sizeof(uint32_t))};
  m_dstCmdRec.list->SetComputeRoot32BitConstants(0, 2, &shaderInput, 0);
  m_dstCmdRec.list->SetComputeRootUnorderedAccessView(1, m_dstBuffer->GetGPUVirtualAddress());
  m_dstCmdRec.list->SetComputeRootUnorderedAccessView(2, m_errorCountBuffer->GetGPUVirtualAddress());
  m_dstCmdRec.list->Dispatch((shaderInput.dataBufferSize + 1023) / 1024, 1, 1);
}

Transfer::Result D3D12Transfer::readbackResult() const {
  uint64_t freq = 0;
  GBB_THROW_UNLESS_D3D(m_srcCmdRec.queue->GetTimestampFrequency(&freq));
  auto ticks = static_cast<double>(m_timestampMappedPtr[1] - m_timestampMappedPtr[0]);
  return {.errorCount = m_errorCountMappedPtr[0],
          .duration = std::chrono::duration<double>(ticks / static_cast<double>(freq))};
}
} // namespace gbb