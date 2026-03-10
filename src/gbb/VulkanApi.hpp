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
#pragma once
#include "Api.hpp"

#include <vulkan/vulkan.hpp>

#include <variant>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace gbb {
class VulkanApi : public Api {
public:
  VulkanApi(std::variant<uint32_t, const uint8_t *> p_selectedDevice, bool p_printMemProps);
  ~VulkanApi() = default;

  std::string getName() override { return "Vulkan"; }
  uint32_t getPhysicalDeviceCount() override { return static_cast<uint32_t>(m_physicalDevices.size()); }
  uint32_t getDeviceMaskAll() const { return (1u << static_cast<uint32_t>(m_physicalDevices.size())) - 1u; }
  vk::PhysicalDevice getPhysicalDevice(uint32_t p_index) const { return m_physicalDevices[p_index]; }
  std::unique_ptr<Transfer> createTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx,
                                           DeviceIndex p_dstDevIdx) override;

  uint32_t getPrimaryQueueFamilyIndex() const { return m_primaryQueueFamilyIndex; }
  vk::Queue getPrimaryQueue() const { return m_primaryQueue; }
  vk::Device dev() const { return m_device.get(); }
  const uint8_t *getLuid() const { return m_luid.data(); }
  const uint8_t *getUuid(uint32_t p_deviceIndex) const { return m_uuids[p_deviceIndex].data(); }

private:
  static const uint32_t s_invalidQueueFamilyIndex = static_cast<uint32_t>(-1);

  vk::UniqueInstance m_instance = {};
  uint32_t m_primaryQueueFamilyIndex = s_invalidQueueFamilyIndex;
  vk::UniqueDevice m_device = {};
  vk::Queue m_primaryQueue = {};
  std::vector<vk::PhysicalDevice> m_physicalDevices;
  std::array<uint8_t, vk::LuidSize> m_luid = {};
  std::vector<std::array<uint8_t, vk::UuidSize>> m_uuids;
};

inline std::unique_ptr<VulkanApi> g_vulkan;
} // namespace gbb