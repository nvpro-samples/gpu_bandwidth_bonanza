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
#include "NvmlInterface.hpp"

#include "CudaApi.hpp"
#include "Logger.hpp"
#include "UuidUtils.hpp"

#include <regex>

namespace gbb {
NvmlInterface::NvmlInterface() {
  GBB_THROW_UNLESS_NVML(nvmlInit());
  uint32_t deviceCount = {};
  GBB_THROW_UNLESS_NVML(nvmlDeviceGetCount(&deviceCount));
  for (uint32_t devIdx = 0; devIdx < deviceCount; ++devIdx) {
    nvmlDevice_t device = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetHandleByIndex(devIdx, &device));
    char name[96] = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetName(device, name, std::size(name)));
    char uuid[96] = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetUUID(device, uuid, std::size(uuid)));
    bool selected = false;
    for (uint32_t j = 0; !selected && j < g_cuda->getPhysicalDeviceCount(); ++j) {
      selected = std::string(uuid).ends_with(uuidToString(g_cuda->getUuid(j).bytes));
    }

    GBB_INFO("NVML device {}: {}{}", devIdx, name, selected ? GBB_FORMAT_CYAN(", selected") : "");
    GBB_INFO("├╴UUID: {}", uuid);

    nvmlDeviceAttributes_t attributes = {};
    if (nvmlReturn_t r = nvmlDeviceGetAttributes(device, &attributes); r != NVML_ERROR_NOT_SUPPORTED) {
      GBB_THROW_UNLESS_NVML(r);
      GBB_INFO("├╴SM count: {}", attributes.multiprocessorCount);
      GBB_INFO("├╴Shared copy engine count: {}", attributes.sharedCopyEngineCount);
      GBB_INFO("├╴Shared decoder count: {}", attributes.sharedDecoderCount);
      GBB_INFO("├╴Shared encoder count: {}", attributes.sharedEncoderCount);
      GBB_INFO("├╴Shared JPEG count: {}", attributes.sharedJpegCount);
      GBB_INFO("├╴Shared OFA count: {}", attributes.sharedOfaCount);
      GBB_INFO("├╴GPU instance slice count: {}", attributes.gpuInstanceSliceCount);
      GBB_INFO("├╴Compute instance slice count: {}", attributes.computeInstanceSliceCount);
      GBB_INFO("├╴Memory: {} MiB", attributes.memorySizeMB);
    }

    uint32_t boardId = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetBoardId(device, &boardId));
    GBB_INFO("├╴Board ID: 0x{:x}", boardId);

    struct LinkProperties {
      nvmlEnableState_t active;
      uint32_t valid;
      uint32_t p2p;
      uint32_t sli;
      bool hasRemoteDevice;
      nvmlPciInfo_t remotePciInfo;
      nvmlIntNvLinkDeviceType_t remoteDeviceType;
    };
    std::vector<LinkProperties> links;
    for (uint32_t link = 0;; ++link) {
      LinkProperties props = {};
      nvmlReturn_t result = nvmlDeviceGetNvLinkState(device, link, &props.active);
      if (result == NVML_ERROR_NOT_SUPPORTED) {
        break;
      }
      GBB_THROW_UNLESS_NVML(result);
      GBB_THROW_UNLESS_NVML(nvmlDeviceGetNvLinkCapability(device, link, NVML_NVLINK_CAP_VALID, &props.valid));
      GBB_THROW_UNLESS_NVML(nvmlDeviceGetNvLinkCapability(device, link, NVML_NVLINK_CAP_P2P_SUPPORTED, &props.p2p));
      GBB_THROW_UNLESS_NVML(nvmlDeviceGetNvLinkCapability(device, link, NVML_NVLINK_CAP_SLI_BRIDGE, &props.sli));
      result = nvmlDeviceGetNvLinkRemotePciInfo_v2(device, link, &props.remotePciInfo);
      if (result != NVML_ERROR_NOT_SUPPORTED) {
        GBB_THROW_UNLESS_NVML(result);
        GBB_THROW_UNLESS_NVML(nvmlDeviceGetNvLinkRemoteDeviceType(device, link, &props.remoteDeviceType));
        props.hasRemoteDevice = true;
      }
      links.emplace_back(props);
    }

    uint32_t speed = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetPcieSpeed(device, &speed));
    uint32_t maxLinkGen = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetMaxPcieLinkGeneration(device, &maxLinkGen));
    uint32_t maxLinkGenDevice = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetGpuMaxPcieLinkGeneration(device, &maxLinkGenDevice));
    uint32_t maxLinkWidth = {};
    GBB_THROW_UNLESS_NVML(nvmlDeviceGetMaxPcieLinkWidth(device, &maxLinkWidth));
    // GBB_INFO("├╴PCIe speed: {} GiB/s", static_cast<float>(speed) / 1024.0f);
    GBB_INFO("{}╴System: PCIe {}.0 x{}, GPU-only: PCIe {}.0 x{}", links.empty() ? "└" : "├", maxLinkGen, maxLinkWidth,
             maxLinkGenDevice, maxLinkWidth);

    uint32_t link = 0;
    for (uint32_t link = 0; link < links.size();) {
      const LinkProperties &props = links[link];
      std::string linkIndices = std::to_string(link);
      while (++link < links.size() && memcmp(&links[link - 1], &links[link], sizeof(LinkProperties)) == 0) {
        linkIndices += fmt::format(",{}", link);
      }
      bool isLast = link == links.size();
      GBB_INFO("{}╴NvLink [{}], supported: {}, P2P supported: {}, SLI supported: {}, active: {}", isLast ? "└" : "├",
               linkIndices, props.valid ? "yes" : "no", props.p2p ? "yes" : "no", props.sli ? "yes" : "no",
               props.active == NVML_FEATURE_ENABLED ? "yes" : "no");
      if (!props.hasRemoteDevice) {
        GBB_INFO("{} └╴Remote device: none", isLast ? " " : "│");
      } else {
        std::string deviceType;
        switch (props.remoteDeviceType) {
        case NVML_NVLINK_DEVICE_TYPE_GPU: deviceType = "GPU"; break;
        case NVML_NVLINK_DEVICE_TYPE_IBMNPU: deviceType = "IBMNPU"; break;
        case NVML_NVLINK_DEVICE_TYPE_SWITCH: deviceType = "SWITCH"; break;
        case NVML_NVLINK_DEVICE_TYPE_UNKNOWN: deviceType = "UNKNOWN"; break;
        default: deviceType = fmt::format("unknown (0x{:x})", static_cast<uint32_t>(props.remoteDeviceType));
        }
        GBB_INFO("{} └╴Remote device, type: {}, bus id: {}", isLast ? " " : "│", deviceType, props.remotePciInfo.busId);
      }
    }
  }
}

NvmlInterface::~NvmlInterface() { nvmlShutdown(); }

#define GBB_NVML_LOG(_fn)                                                                                              \
  do {                                                                                                                 \
    std::string _resultString;                                                                                         \
    try {                                                                                                              \
      _resultString = gbb::toString(_fn);                                                                              \
    } catch (const Exception &ex) {                                                                                    \
      _resultString = ex.getMessage();                                                                                 \
    }                                                                                                                  \
    GBB_INFO("{}: {}", modifyNvmlCommand(#_fn), _resultString);                                                        \
  } while (0)

std::string modifyNvmlCommand(const std::string p_cmd) {
  std::regex p("([^\\.]+)\\.([^\\(]+)\\(([^\\)]*)\\)");
  std::smatch smatch;
  if (std::regex_match(p_cmd, smatch, p) && smatch.size() == 4) {
    std::string result = std::string("nvml") + smatch[1].str() + smatch[2].str();
    result[4] = std::toupper(result[4]);
    result[4 + smatch[1].str().size()] = std::toupper(result[4 + smatch[1].str().size()]);
    if (!smatch[3].str().empty()) {
      result += ", " + smatch[3].str();
    }
    return result;
  }
  return p_cmd;
}
} // namespace gbb
