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
#include "CudaTransfer.hpp"

#include "CudaApi.hpp"
#include "Logger.hpp"

namespace gbb {
__device__ uint32_t rng(uint32_t p_prev) {
  const uint64_t m = 1ull << 31;
  const uint64_t a = 1103515245;
  const uint64_t c = 12345;
  return static_cast<uint32_t>((a * static_cast<uint64_t>(p_prev) + c) % m);
}

__global__ void genData(uint32_t p_seed, uint32_t p_bufferSize, uint32_t *p_buffer) {
  uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (p_bufferSize <= idx) {
    return;
  }
  p_buffer[idx] = rng(p_seed + idx);
}

__global__ void checkData(uint32_t p_seed, uint32_t p_bufferSize, const uint32_t *p_buffer, uint32_t *p_errorCount) {
  uint32_t idx = blockIdx.x * blockDim.x + threadIdx.x;
  if (p_bufferSize <= idx) {
    return;
  }
  if (p_buffer[idx] != rng(p_seed + idx)) {
    atomicInc(p_errorCount, p_bufferSize + 1);
  }
}

void CudaTransfer::CudaMemAllocation::alloc(size_t p_byteSize) {
  GBB_THROW_UNLESS_CUDA(cuMemAlloc(&devPtr, p_byteSize));
  hostPtr = nullptr;
}

void CudaTransfer::CudaMemAllocation::allocHost(size_t p_byteSize) {
  GBB_THROW_UNLESS_CUDA(cuMemAllocHost(&hostPtr, p_byteSize));
  GBB_THROW_UNLESS_CUDA(cuMemHostGetDevicePointer(&devPtr, hostPtr, 0));
}

void CudaTransfer::CudaMemAllocation::free() {
  if (hostPtr) {
    GBB_THROW_UNLESS_CUDA(cuMemFreeHost(hostPtr));
  } else {
    GBB_THROW_UNLESS_CUDA(cuMemFree(devPtr));
  }
  devPtr = {};
  hostPtr = nullptr;
}

CudaTransfer::CudaTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx)
    : Transfer(p_byteSize, p_srcDevIdx, p_dstDevIdx),
      m_bufferSize(static_cast<uint32_t>(p_byteSize / sizeof(uint32_t))) {
  GBB_THROW_UNLESS(p_byteSize % sizeof(uint32_t) == 0,
                   "Byte size ({}) must be a multiple of sizeof(uint32_t), which is {}.", p_byteSize, sizeof(uint32_t));

  m_srcCtx = g_cuda->getContext(p_srcDevIdx.getOrIfHost(p_dstDevIdx.getOrIfHost(0)));
  m_dstCtx = g_cuda->getContext(p_dstDevIdx.getOrIfHost(p_srcDevIdx.getOrIfHost(0)));

  GBB_THROW_UNLESS_CUDA(cuCtxPushCurrent(m_srcCtx));
  if (this->getSrcDevIdx().isHost()) {
    m_srcMem.allocHost(p_byteSize);
  } else {
    m_srcMem.alloc(p_byteSize);
  }
  GBB_THROW_UNLESS_CUDA(cuEventCreate(&m_evtA, CU_EVENT_DEFAULT));
  GBB_THROW_UNLESS_CUDA(cuEventCreate(&m_evtB, CU_EVENT_DEFAULT));
  GBB_THROW_UNLESS_CUDA(cuCtxPopCurrent(nullptr));

  GBB_THROW_UNLESS_CUDA(cuCtxPushCurrent(m_dstCtx));
  if (this->getDstDevIdx().isHost()) {
    m_dstMem.allocHost(p_byteSize);
  } else {
    m_dstMem.alloc(p_byteSize);
  }
  m_errorCountMem.alloc(sizeof(uint32_t));
  GBB_THROW_UNLESS_CUDA(cuCtxPopCurrent(nullptr));

  if (!p_srcDevIdx.isHost()) {
    CUdevice srcDev = {};
    GBB_THROW_UNLESS_CUDA(cuDeviceGet(&srcDev, p_srcDevIdx.get()));
    int32_t unifiedAddressing;
    GBB_THROW_UNLESS_CUDA(cuDeviceGetAttribute(&unifiedAddressing, CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING, srcDev));
    GBB_THROW_UNLESS(unifiedAddressing, "Unified adressing not supported by device {}", p_srcDevIdx.get());
  }

  if (!p_dstDevIdx.isHost()) {
    CUdevice dstDev = {};
    GBB_THROW_UNLESS_CUDA(cuDeviceGet(&dstDev, p_dstDevIdx.get()));
    int32_t unifiedAddressing;
    GBB_THROW_UNLESS_CUDA(cuDeviceGetAttribute(&unifiedAddressing, CU_DEVICE_ATTRIBUTE_UNIFIED_ADDRESSING, dstDev));
    GBB_THROW_UNLESS(unifiedAddressing, "Unified adressing not supported by device {}", p_dstDevIdx.get());
  }
}

CudaTransfer::~CudaTransfer() {
  m_errorCountMem.free();
  m_srcMem.free();
  m_dstMem.free();
  cuEventDestroy(m_evtB);
  cuEventDestroy(m_evtA);
}

Transfer::Result CudaTransfer::execute(uint32_t p_seed) {
  // generate data
  GBB_THROW_UNLESS_CUDA(cuCtxPushCurrent(m_srcCtx));
  const uint32_t threadsPerBlock = 1024;
  const uint32_t blockCount = (m_bufferSize + threadsPerBlock - 1) / threadsPerBlock;
  genData<<<blockCount, threadsPerBlock>>>(p_seed, m_bufferSize, reinterpret_cast<uint32_t *>(m_srcMem.devPtr));
  GBB_THROW_UNLESS_CUDA(cudaGetLastError());

  // execute copy
  GBB_THROW_UNLESS_CUDA(cuEventRecord(m_evtA, 0));
  if (m_dstMem.hostPtr && !m_srcMem.hostPtr) {
    GBB_THROW_UNLESS_CUDA(cuMemcpyDtoHAsync(m_dstMem.hostPtr, m_srcMem.devPtr, this->getByteSize(), 0));
  } else if (!m_dstMem.hostPtr && m_srcMem.hostPtr) {
    GBB_THROW_UNLESS_CUDA(cuMemcpyHtoDAsync(m_dstMem.devPtr, m_srcMem.hostPtr, this->getByteSize(), 0));
  } else if (this->getSrcDevIdx().isHost() || this->getDstDevIdx().isHost()) {
    GBB_THROW_UNLESS_CUDA(cuMemcpyAsync(m_dstMem.devPtr, m_srcMem.devPtr, this->getByteSize(), 0));
  } else {
    GBB_THROW_UNLESS_CUDA(cuMemcpyPeerAsync(m_dstMem.devPtr, g_cuda->getContext(this->getDstDevIdx().get()),
                                            m_srcMem.devPtr, g_cuda->getContext(this->getSrcDevIdx().get()),
                                            this->getByteSize(), 0));
    // GBB_THROW_UNLESS_CUDA(cuMemcpyAsync(m_dstMem.devPtr, m_srcMem.devPtr, this->getByteSize(), 0));
  }
  GBB_THROW_UNLESS_CUDA(cuEventRecord(m_evtB, 0));
  GBB_THROW_UNLESS_CUDA(cuEventSynchronize(m_evtB));
  GBB_THROW_UNLESS_CUDA(cuCtxPopCurrent(nullptr));

  // check data
  GBB_THROW_UNLESS_CUDA(cuCtxPushCurrent(m_dstCtx));
  GBB_THROW_UNLESS_CUDA(cuMemsetD32Async(m_errorCountMem.devPtr, 0, 1, 0));
  checkData<<<blockCount, threadsPerBlock>>>(p_seed, m_bufferSize, reinterpret_cast<const uint32_t *>(m_dstMem.devPtr),
                                             reinterpret_cast<uint32_t *>(m_errorCountMem.devPtr));
  GBB_THROW_UNLESS_CUDA(cuStreamSynchronize(0));

  // process results
  Result result;
  GBB_THROW_UNLESS_CUDA(cuMemcpyDtoH(&result.errorCount, m_errorCountMem.devPtr, sizeof(uint32_t)));
  float durationMillis = 0.0f;
  GBB_THROW_UNLESS_CUDA(cuEventElapsedTime(&durationMillis, m_evtA, m_evtB));
  result.duration = std::chrono::duration<double>(1e-3 * static_cast<double>(durationMillis));
  GBB_THROW_UNLESS_CUDA(cuCtxPopCurrent(nullptr));
  return result;
}
} // namespace gbb