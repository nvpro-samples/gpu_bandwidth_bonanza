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
#include "Options.hpp"

#include "Logger.hpp"

#include <chrono>

namespace gbb {
void Options::printUsage() {
  std::string optNoCuda = "";
  std::string optNoD3D12 = "";
#ifdef GBB_CUDA_ENABLED
  optNoCuda = " [--no-cuda]";
#endif
#ifdef GBB_D3D12_ENABLED
  optNoD3D12 = " [--no-d3d12]";
#endif
  GBB_INFO("Usage:");
  GBB_INFO("  " APP_NAME " --help | -h");
  GBB_INFO("  " APP_NAME " --version | -v");
  GBB_INFO("  " APP_NAME " [--vulkan-device-group <index> | --no-vulkan]{}{} [--duration <millis>] [--transfer-size "
           "<mib>] [--output <path>] [--print-vulkan-mem-props]",
           optNoD3D12, optNoCuda);
#ifdef GBB_D3D12_ENABLED
  GBB_INFO("  " APP_NAME " [--dxgi-adapter <index> | --no-d3d12] [--no-vulkan]{} [--duration "
           "<millis>] [--transfer-size <mib>] [--output <path>] [--print-vulkan-mem-props]",
           optNoCuda);
#endif
  GBB_INFO("Options:");
  GBB_INFO("  --help, -h                    │ Shows this text");
  GBB_INFO("  --version, -v                 │ Shows version and build information");
  GBB_INFO("  --vulkan-device-group <index> │ Selects Vulkan device group by index");
#ifdef GBB_D3D12_ENABLED
  GBB_INFO("  --dxgi-adapter <index>        │ Selects DXGI adapter by index");
#endif
  GBB_INFO("  --no-vulkan                   │ Disables Vulkan");
#ifdef GBB_CUDA_ENABLED
  GBB_INFO("  --no-cuda                     │ Disables CUDA and NVML");
#endif
#ifdef GBB_D3D12_ENABLED
  GBB_INFO("  --no-d3d12                    │ Disables D3D12");
#endif
  GBB_INFO("  --print-vulkan-mem-props      │ Prints memory properties for each physical Vulkan device");
  GBB_INFO("  --duration <millis>           │ Sets the measurement duration in milliseconds per API and transfer "
           "direction; default: 1000");
  GBB_INFO("  --transfer-size <mib>         │ Sets the chunk size per individual transfer in MiB; default: 256");
  GBB_INFO("  --output <path>               │ Writes the results to a CSV file");
  GBB_INFO("gpu_bandwidth_bonanza runs transfers of --transfer-size sized memory chunks for --duration milliseconds "
           "per API and direction. The average transfer speeds will be reported at the end. Devices can either be "
           "specified by Vulkan device group index or by DXGI adapter index. All supported devices will be tested if "
           "CUDA is the only enabled API.");
}

Options Options::fromArgs(const std::vector<std::string> &p_args) {
  Options options;
  for (auto it = p_args.begin() + 1; it != p_args.end(); ++it) {
    if (*it == "--vulkan-device-group") {
      GBB_THROW_IF(++it == p_args.end(), "No value provided for --vulkan-device-group option.");
      try {
        options.vulkanDeviceGroupIndex = static_cast<uint32_t>(std::stoul(*it));
      } catch (const std::invalid_argument &ex) {
        (void)ex;
        GBB_THROW("Invalid value provided for --vulkan-device-group option.");
      }
    } else if (*it == "--dxgi-adapter") {
      GBB_THROW_IF(++it == p_args.end(), "No value provided for --dxgi-adapter option.");
      try {
        options.dxgiAdapterIndex = static_cast<uint32_t>(std::stoul(*it));
      } catch (const std::invalid_argument &ex) {
        (void)ex;
        GBB_THROW("Invalid value provided for --dxgi-adapter option.");
      }
    } else if (*it == "--no-vulkan") {
      options.noVulkan = true;
#ifdef GBB_CUDA_ENABLED
    } else if (*it == "--no-cuda") {
      options.noCuda = true;
#endif
#ifdef GBB_D3D12_ENABLED
    } else if (*it == "--no-d3d12") {
      options.noD3D12 = true;
#endif
    } else if (*it == "--print-vulkan-mem-props") {
      options.printVulkanMemProps = true;
    } else if (*it == "--duration") {
      GBB_THROW_UNLESS(++it != p_args.end(), "No value provided for --duration option.");
      try {
        options.durationPerDirection = std::chrono::milliseconds(std::stoi(*it));
      } catch (...) {
        GBB_THROW("Invalid value provided for --duration option.");
      }
    } else if (*it == "--transfer-size") {
      GBB_THROW_UNLESS(++it != p_args.end(), "No value provided for --transfer-size option.");
      try {
        options.byteSizePerTransfer = static_cast<size_t>(std::stoull(*it)) << 20;
      } catch (...) {
        GBB_THROW("Invalid value provided for --transfer-size option.");
      }
    } else if (*it == "--output") {
      GBB_THROW_UNLESS(++it != p_args.end(), "No value provided for --output option.");
      options.output = *it;
    } else {
      GBB_WARN("Unknown option: {}", *it);
    }
  }
  return options;
}
} // namespace gbb