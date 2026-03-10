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
#include "VulkanTransfer.hpp"

#include "Logger.hpp"
#include "VulkanApi.hpp"

#include <vector>

#include "shaders/gbb.slang.spv.h"

namespace gbb {
struct ShaderInput {
  uint32_t seed;
  uint32_t dataBufferSize;
};

VulkanTransfer::VulkanTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx)
    : Transfer(p_byteSize, p_srcDevIdx, p_dstDevIdx),
      m_fallbackDevIdx(this->getSrcDevIdx().getOrIfHost(this->getDstDevIdx().getOrIfHost(0))) {
  GBB_THROW_UNLESS(p_byteSize % sizeof(uint32_t) == 0,
                   "Byte size ({}) must be a multiple of sizeof(uint32_t), which is {}.", p_byteSize, sizeof(uint32_t));

  m_cmdPool =
      g_vulkan->dev().createCommandPoolUnique(vk::CommandPoolCreateInfo({}, g_vulkan->getPrimaryQueueFamilyIndex()));
  auto cmdBuffers = g_vulkan->dev().allocateCommandBuffersUnique(
      vk::CommandBufferAllocateInfo(m_cmdPool.get(), vk::CommandBufferLevel::ePrimary, 2));
  m_cmdBufferA = std::move(cmdBuffers[0]);
  m_cmdBufferB = std::move(cmdBuffers[1]);

  m_srcBuffer = this->allocateBuffer(this->getSrcDevIdx(), this->getByteSize(),
                                     vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc);
  m_dstBuffer = this->allocateBuffer(this->getDstDevIdx(), this->getByteSize(),
                                     vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
  m_errorCountBuffer =
      this->allocateBuffer(DeviceIndex::HOST, sizeof(uint32_t), vk::BufferUsageFlagBits::eStorageBuffer);
  m_errorCountMappedPtr =
      reinterpret_cast<uint32_t *>(g_vulkan->dev().mapMemory(m_errorCountBuffer.mem.get(), 0, sizeof(uint32_t)));

  m_queryPool = g_vulkan->dev().createQueryPoolUnique(vk::QueryPoolCreateInfo({}, vk::QueryType::eTimestamp, 2));

  std::vector<vk::DescriptorSetLayoutBinding> setLayoutBindings = {
      vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute),
      vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute)};
  m_descriptorSetLayout =
      g_vulkan->dev().createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo({}, setLayoutBindings));
  vk::PushConstantRange pushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(ShaderInput));
  m_pipelineLayout = g_vulkan->dev().createPipelineLayoutUnique(
      vk::PipelineLayoutCreateInfo({}, m_descriptorSetLayout.get(), pushConstantRange));

  vk::UniqueShaderModule shaderModule =
      g_vulkan->dev().createShaderModuleUnique(vk::ShaderModuleCreateInfo({}, gbb_slang_spv));

  std::vector<vk::ComputePipelineCreateInfo> pipelineCreateInfos = {
      vk::ComputePipelineCreateInfo(
          {}, vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, shaderModule.get(), "gen"),
          m_pipelineLayout.get()),
      vk::ComputePipelineCreateInfo(
          {}, vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eCompute, shaderModule.get(), "check"),
          m_pipelineLayout.get())};
  auto [result, pipelines] = g_vulkan->dev().createComputePipelinesUnique({}, pipelineCreateInfos);
  m_genPipeline = std::move(pipelines[0]);
  m_checkPipeline = std::move(pipelines[1]);

  m_semaphore = g_vulkan->dev().createSemaphoreUnique({});

  vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer, 2);
  m_descriptorPool = g_vulkan->dev().createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo({}, 2, poolSize));
  std::vector<vk::DescriptorSetLayout> setLayouts = {m_descriptorSetLayout.get(), m_descriptorSetLayout.get()};
  std::vector<vk::DescriptorSet> descriptorSets =
      g_vulkan->dev().allocateDescriptorSets(vk::DescriptorSetAllocateInfo(m_descriptorPool.get(), setLayouts));
  m_genDescriptorSet = descriptorSets[0];
  m_checkDescriptorSet = descriptorSets[1];

  vk::DescriptorBufferInfo srcBufferDescInfo(m_srcBuffer.buffer.get(), 0, this->getByteSize());
  vk::DescriptorBufferInfo dstBufferDescInfo(m_dstBuffer.buffer.get(), 0, this->getByteSize());
  vk::DescriptorBufferInfo errBufferDescInfo(m_errorCountBuffer.buffer.get(), 0, sizeof(uint32_t));
  std::vector<vk::WriteDescriptorSet> descriptorWrites = {
      vk::WriteDescriptorSet(m_genDescriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, {}, &srcBufferDescInfo),
      vk::WriteDescriptorSet(m_checkDescriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, {}, &dstBufferDescInfo),
      vk::WriteDescriptorSet(m_checkDescriptorSet, 1, 0, 1, vk::DescriptorType::eStorageBuffer, {},
                             &errBufferDescInfo)};
  g_vulkan->dev().updateDescriptorSets(descriptorWrites, {});
}

VulkanTransfer::~VulkanTransfer() {
  try {
    g_vulkan->dev().unmapMemory(m_errorCountBuffer.mem.get());
  } catch (const vk::SystemError &ex) {
    GBB_ERROR("{}", ex.what());
  }
}

VulkanTransfer::BufferContainer VulkanTransfer::allocateBuffer(DeviceIndex p_devIdx, size_t p_byteSize,
                                                               vk::BufferUsageFlags p_usage) {
  vk::PhysicalDeviceMemoryProperties physicalDeviceMemProps =
      g_vulkan->getPhysicalDevice(p_devIdx.getOrIfHost(0)).getMemoryProperties();
  std::optional<uint32_t> memTypeIdx = {};
  for (uint32_t i = 0; i < physicalDeviceMemProps.memoryTypeCount; ++i) {
    bool isDeviceLocal =
        (bool)(physicalDeviceMemProps.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal);
    bool isHostVisible =
        (bool)(physicalDeviceMemProps.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible);
    if ((!p_devIdx.isHost() && isDeviceLocal) || (p_devIdx.isHost() && !isDeviceLocal && isHostVisible)) {
      memTypeIdx = i;
      break;
    }
  }
  GBB_THROW_UNLESS(memTypeIdx, "No suitable mem type found.");
  vk::StructureChain memAllocInfo(
      vk::MemoryAllocateInfo(this->getByteSize(), memTypeIdx.value()),
      vk::MemoryAllocateFlagsInfo(vk::MemoryAllocateFlagBits::eDeviceMask, 1u << p_devIdx.getOrIfHost(0)));
  vk::UniqueDeviceMemory mem = g_vulkan->dev().allocateMemoryUnique(memAllocInfo.get());
  vk::UniqueBuffer buffer = g_vulkan->dev().createBufferUnique(
      vk::BufferCreateInfo({}, this->getByteSize(), p_usage, vk::SharingMode::eExclusive));
  g_vulkan->dev().bindBufferMemory(buffer.get(), mem.get(), 0);
  return {.mem = std::move(mem), .buffer = std::move(buffer)};
}

Transfer::Result VulkanTransfer::execute(uint32_t p_seed) {
  g_vulkan->dev().resetCommandPool(m_cmdPool.get());
  m_cmdBufferA->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
  m_cmdBufferA->resetQueryPool(m_queryPool.get(), 0, 2);
  vk::BufferMemoryBarrier2 preGenBufferMemoryBarrier(
      vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone, vk::PipelineStageFlagBits2::eComputeShader,
      vk::AccessFlagBits2::eShaderWrite, vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, m_srcBuffer.buffer.get(), 0,
      this->getByteSize());
  m_cmdBufferA->pipelineBarrier2(vk::DependencyInfo({}, {}, preGenBufferMemoryBarrier));
  m_cmdBufferA->bindPipeline(vk::PipelineBindPoint::eCompute, m_genPipeline.get());
  ShaderInput genShaderInput = {.seed = p_seed,
                                .dataBufferSize = static_cast<uint32_t>(this->getByteSize() / sizeof(uint32_t))};
  m_cmdBufferA->pushConstants(m_pipelineLayout.get(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(ShaderInput),
                              &genShaderInput);
  m_cmdBufferA->bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout.get(), 0, m_genDescriptorSet, {});
  m_cmdBufferA->dispatch((genShaderInput.dataBufferSize + 1023) / 1024, 1, 1);

  std::vector<vk::BufferMemoryBarrier2> preTransferBufferMemoryBarriers = {
      vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderWrite,
                               vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead,
                               vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, m_srcBuffer.buffer.get(), 0,
                               this->getByteSize()),
      vk::BufferMemoryBarrier2(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eNone,
                               vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
                               vk::QueueFamilyIgnored, vk::QueueFamilyIgnored, m_dstBuffer.buffer.get(), 0,
                               this->getByteSize())};
  m_cmdBufferA->pipelineBarrier2(
      vk::DependencyInfo(vk::DependencyFlagBits::eDeviceGroup, {}, preTransferBufferMemoryBarriers));

  m_cmdBufferB->begin(vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

  vk::CommandBuffer copyCmdBuffer = m_cmdBufferB.get();
  copyCmdBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eAllCommands, m_queryPool.get(), 0);
  copyCmdBuffer.copyBuffer(m_srcBuffer.buffer.get(), m_dstBuffer.buffer.get(),
                           vk::BufferCopy(0, 0, this->getByteSize()));
  copyCmdBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eTransfer, m_queryPool.get(), 1);

  m_cmdBufferA->end();

  vk::BufferMemoryBarrier2 preCheckBufferMemoryBarrier(
      vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite,
      vk::PipelineStageFlagBits2::eComputeShader, vk::AccessFlagBits2::eShaderRead, vk::QueueFamilyIgnored,
      vk::QueueFamilyIgnored, m_dstBuffer.buffer.get(), 0, this->getByteSize());
  m_cmdBufferB->pipelineBarrier2(vk::DependencyInfo({}, {}, preCheckBufferMemoryBarrier));
  m_cmdBufferB->bindPipeline(vk::PipelineBindPoint::eCompute, m_checkPipeline.get());
  ShaderInput checkShaderInput = {.seed = p_seed,
                                  .dataBufferSize = static_cast<uint32_t>(this->getByteSize() / sizeof(uint32_t))};
  m_cmdBufferB->pushConstants(m_pipelineLayout.get(), vk::ShaderStageFlagBits::eCompute, 0, sizeof(ShaderInput),
                              &checkShaderInput);
  m_cmdBufferB->bindDescriptorSets(vk::PipelineBindPoint::eCompute, m_pipelineLayout.get(), 0, m_checkDescriptorSet,
                                   {});
  m_errorCountMappedPtr[0] = 0;
  m_cmdBufferB->dispatch((checkShaderInput.dataBufferSize + 1023) / 1024, 1, 1);
  m_cmdBufferB->end();

  std::vector<vk::CommandBufferSubmitInfo> submitInfos = {
      vk::CommandBufferSubmitInfo(m_cmdBufferA.get(), 1u << this->getSrcDevIdx().getOrIfHost(m_fallbackDevIdx)),
      vk::CommandBufferSubmitInfo(m_cmdBufferB.get(), 1u << this->getDstDevIdx().getOrIfHost(m_fallbackDevIdx))};
  std::vector<vk::SemaphoreSubmitInfo> semInfos = {
      vk::SemaphoreSubmitInfo(m_semaphore.get(), 0, vk::PipelineStageFlagBits2::eTransfer,
                              this->getSrcDevIdx().getOrIfHost(m_fallbackDevIdx)),
      vk::SemaphoreSubmitInfo(m_semaphore.get(), 0, vk::PipelineStageFlagBits2::eAllCommands,
                              this->getDstDevIdx().getOrIfHost(m_fallbackDevIdx))};
  std::vector<vk::SubmitInfo2> submits = {vk::SubmitInfo2({}, {}, submitInfos[0], semInfos[0]),
                                          vk::SubmitInfo2({}, semInfos[1], submitInfos[1])};
  g_vulkan->getPrimaryQueue().submit2(submits);
  g_vulkan->dev().waitIdle();
  auto [result, timestamps] = g_vulkan->dev().getQueryPoolResults<uint64_t>(
      m_queryPool.get(), 0, 2, 2 * sizeof(uint64_t), sizeof(uint64_t), vk::QueryResultFlagBits::eWait);
  return {.errorCount = m_errorCountMappedPtr[0],
          .duration = std::chrono::duration<double>(1e-9 * static_cast<double>(timestamps[1] - timestamps[0]))};
}
} // namespace gbb
