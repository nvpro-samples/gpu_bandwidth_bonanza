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
#include "VulkanApi.hpp"

#include "Logger.hpp"
#include "UuidUtils.hpp"
#include "VulkanTransfer.hpp"

#include <variant>

namespace gbb {
VulkanApi::VulkanApi(std::variant<uint32_t, const uint8_t *> p_selectedDevice, bool p_printMemProps) {
  std::vector<const char *> instanceLayers = {};
  std::vector<const char *> instanceExtensions = {};
  std::vector<const char *> deviceLayers = {};
  std::vector<const char *> deviceExtensions = {};
  vk::ApplicationInfo appInfo(APP_NAME, 0, nullptr, 0, vk::ApiVersion14);
  vk::InstanceCreateInfo instanceCreateInfo({}, &appInfo, instanceLayers, instanceExtensions);
  m_instance = vk::createInstanceUnique(instanceCreateInfo, nullptr);
  std::vector<vk::PhysicalDeviceGroupProperties> deviceGroups = m_instance->enumeratePhysicalDeviceGroups();
  GBB_INFO("Vulkan device groups{}", deviceGroups.empty() ? ": none" : "");

  std::optional<uint32_t> selectedDeviceGroupIndex = std::holds_alternative<uint32_t>(p_selectedDevice)
                                                         ? std::make_optional(std::get<uint32_t>(p_selectedDevice))
                                                         : std::nullopt;

  for (size_t i = 0; i < deviceGroups.size(); ++i) {
    if (!selectedDeviceGroupIndex) {
      // Vulkan specification ensures that all physical devices within a device group share the same Locally Unique
      // Identifier (LUID) if their LUIDs are valid
      const vk::PhysicalDeviceIDProperties &idProps =
          deviceGroups[i]
              .physicalDevices.front()
              .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>()
              .get<vk::PhysicalDeviceIDProperties>();
      if (idProps.deviceLUIDValid &&
          memcmp(std::get<const uint8_t *>(p_selectedDevice), idProps.deviceLUID, vk::LuidSize) == 0) {
        selectedDeviceGroupIndex = static_cast<uint32_t>(i);
      }
    }
    bool selected = selectedDeviceGroupIndex && selectedDeviceGroupIndex.value() == i;

    const vk::PhysicalDeviceGroupProperties &devGroup = deviceGroups[i];
    GBB_INFO("{}╴[{}] {} physical device{}{}", i == deviceGroups.size() - 1 ? "└" : "├", i,
             deviceGroups[i].physicalDeviceCount, devGroup.physicalDeviceCount == 1 ? "" : "s",
             selected ? ", " + GBB_FORMAT_CYAN("selected") : "");
    std::string prefix1 = fmt::format("{}  ", i == deviceGroups.size() - 1 ? " " : "│");
    for (uint32_t devIdx = 0; devIdx < devGroup.physicalDeviceCount; ++devIdx) {
      vk::PhysicalDevice dev = devGroup.physicalDevices[devIdx];
      auto [props, idProps] = deviceGroups[i]
                                  .physicalDevices[devIdx]
                                  .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>();

      GBB_INFO("{}{}╴{}, uuid [{}], luid [{}]", prefix1, devIdx == devGroup.physicalDeviceCount - 1 ? "└" : "├",
               props.properties.deviceName.data(), uuidToString(idProps.deviceUUID.data()),
               luidToString(idProps.deviceLUID.data()));
      std::string prefix2 = fmt::format("{}{} ", prefix1, devIdx == devGroup.physicalDeviceCount - 1 ? " " : "│");
      if (p_printMemProps) {
        vk::PhysicalDeviceMemoryProperties memProps = dev.getMemoryProperties();
        GBB_INFO("{}└╴{} memory heaps", prefix2, memProps.memoryHeapCount);

        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
          GBB_INFO("{}  {}╴{:.2f} GiB, {}", prefix2, i + 1 == memProps.memoryHeapCount ? "└" : "├",
                   static_cast<float>(memProps.memoryHeaps[i].size) / (1024.0f * 1024.0f * 1024.0f),
                   vk::to_string(static_cast<vk::MemoryHeapFlags>(memProps.memoryHeaps[i].flags)));
          std::string prefix3 = fmt::format("{}  {} ", prefix2, i + 1 == memProps.memoryHeapCount ? " " : "│");
          std::vector<std::pair<uint32_t, vk::MemoryType>> memTypes;
          for (uint32_t j = 0; j < memProps.memoryTypeCount; ++j) {
            if (memProps.memoryTypes[j].heapIndex == i) {
              memTypes.emplace_back(j, memProps.memoryTypes[j]);
            }
          }
          for (size_t j = 0; j < memTypes.size(); ++j) {
            GBB_INFO("{}{}╴mem type index: {}, {}", prefix3, j + 1 == memTypes.size() ? "└" : "├", memTypes[j].first,
                     vk::to_string(static_cast<vk::MemoryPropertyFlags>(memTypes[j].second.propertyFlags)));
          }
        }
      }
    }
  }
  GBB_THROW_IF(!selectedDeviceGroupIndex, "No matching device group found.");
  GBB_THROW_UNLESS(selectedDeviceGroupIndex.value() < deviceGroups.size(),
                   "Given device group index ({}) must be less than the number of device groups ({}).",
                   selectedDeviceGroupIndex.value(), deviceGroups.size());
  m_physicalDevices.resize(deviceGroups[selectedDeviceGroupIndex.value()].physicalDeviceCount);
  m_uuids.resize(m_physicalDevices.size());
  for (size_t i = 0; i < m_physicalDevices.size(); ++i) {
    m_physicalDevices[i] = deviceGroups[selectedDeviceGroupIndex.value()].physicalDevices[i];
    const vk::PhysicalDeviceIDProperties &idProps =
        m_physicalDevices[i]
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceIDProperties>()
            .get<vk::PhysicalDeviceIDProperties>();
    m_uuids[i] = idProps.deviceUUID;
    if (i == 0 && idProps.deviceLUIDValid) {
      m_luid = idProps.deviceLUID;
    }
  }

  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = m_physicalDevices.front().getQueueFamilyProperties();
  for (size_t i = 0; i < queueFamilyProperties.size(); ++i) {
    if (queueFamilyProperties[i].queueFlags & vk::QueueFlagBits::eGraphics) {
      m_primaryQueueFamilyIndex = static_cast<uint32_t>(i);
      break;
    }
  }
  GBB_THROW_UNLESS(m_primaryQueueFamilyIndex != s_invalidQueueFamilyIndex, "No suitable queue family found.");
  float queuePriority = 1.0f;
  vk::DeviceQueueCreateInfo queueCreateInfo({}, m_primaryQueueFamilyIndex, 1, &queuePriority);
  vk::StructureChain deviceCreateInfo(
      vk::DeviceCreateInfo({}, queueCreateInfo, deviceLayers, deviceExtensions),
      vk::DeviceGroupDeviceCreateInfo(deviceGroups[selectedDeviceGroupIndex.value()].physicalDeviceCount,
                                      deviceGroups[selectedDeviceGroupIndex.value()].physicalDevices.data()),
      vk::PhysicalDeviceBufferDeviceAddressFeatures(true, false, true),
      vk::PhysicalDeviceSynchronization2Features(true));
  m_device = m_physicalDevices.front().createDeviceUnique(deviceCreateInfo.get());
  m_primaryQueue = m_device->getQueue(m_primaryQueueFamilyIndex, 0);
}

std::unique_ptr<Transfer> VulkanApi::createTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx,
                                                    DeviceIndex p_dstDevIdx) {
  return std::make_unique<VulkanTransfer>(p_byteSize, p_srcDevIdx, p_dstDevIdx);
}
} // namespace gbb