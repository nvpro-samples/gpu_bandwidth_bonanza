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
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gbb {
struct Options {
  static void printUsage();
  static Options fromArgs(const std::vector<std::string> &p_args);

  std::optional<uint32_t> vulkanDeviceGroupIndex;
  std::optional<uint32_t> dxgiAdapterIndex;
  bool noVulkan = false;
  bool noCuda = false;
  bool noD3D12 = false;
  bool printVulkanMemProps = false;
  std::chrono::milliseconds durationPerDirection = std::chrono::milliseconds(1000);
  std::optional<std::filesystem::path> output = {};
};
} // namespace gbb