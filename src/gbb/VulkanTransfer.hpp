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

#include <vulkan/vulkan.hpp>

namespace gbb {
class VulkanTransfer : public Transfer {
public:
  VulkanTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx);
  ~VulkanTransfer();

  std::string getApiName() const override { return "Vulkan"; }
  Result execute(uint32_t p_seed) override;

private:
  struct BufferContainer {
    vk::UniqueDeviceMemory mem = {};
    vk::UniqueBuffer buffer = {};
  };

  uint32_t m_fallbackDevIdx;
  vk::UniqueCommandPool m_cmdPool = {};
  vk::UniqueCommandBuffer m_cmdBufferA = {};
  vk::UniqueCommandBuffer m_cmdBufferB = {};
  BufferContainer m_srcBuffer = {};
  BufferContainer m_dstBuffer = {};
  BufferContainer m_errorCountBuffer = {};
  uint32_t *m_errorCountMappedPtr = nullptr;
  vk::UniqueQueryPool m_queryPool = {};
  vk::UniqueDescriptorSetLayout m_descriptorSetLayout = {};
  vk::UniquePipelineLayout m_pipelineLayout = {};
  vk::UniquePipeline m_genPipeline = {};
  vk::UniquePipeline m_checkPipeline = {};
  vk::UniqueSemaphore m_semaphore = {};
  vk::UniqueDescriptorPool m_descriptorPool = {};
  vk::DescriptorSet m_genDescriptorSet = {};
  vk::DescriptorSet m_checkDescriptorSet = {};

  BufferContainer allocateBuffer(DeviceIndex p_devIdx, size_t p_byteSize, vk::BufferUsageFlags p_usage);
};
} // namespace gbb
