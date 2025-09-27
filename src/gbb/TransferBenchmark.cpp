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
#include "TransferBenchmark.hpp"

#include "Logger.hpp"

#include <algorithm>
#include <fmt/format.h>
#include <iostream>
#include <vector>

namespace gbb {
TransferBenchmark::Result TransferBenchmark::run(Transfer &p_transfer, std::chrono::milliseconds p_duration) {
  uint32_t runCount = 0;
  std::vector<uint32_t> errorCounts;
  double transferSpeedSum = 0.0;
  auto beg = std::chrono::high_resolution_clock::now();
  for (;; ++runCount) {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = now - beg;
    if (p_duration < elapsed) {
      break;
    }
    Transfer::Result result = p_transfer.execute(3000 + runCount);
    double gigabytes = static_cast<double>(p_transfer.getByteSize()) / static_cast<double>(1 << 30);
    double transferSpeed = gigabytes / result.duration.count();
    std::cout << '\r' << std::string(100, ' ') << '\r'
              << fmt::format("{}{:7.3f} GiB/s; {:4} ms remaining", p_transfer.getLabel(), transferSpeed,
                             std::chrono::duration_cast<std::chrono::milliseconds>(p_duration - elapsed).count());
    transferSpeedSum += transferSpeed;
    errorCounts.emplace_back(result.errorCount);
  }
  std::cout << '\r' << std::string(100, ' ') << '\r';
  double transferSpeed = transferSpeedSum / static_cast<float>(runCount);
  GBB_INFO("{}{:7.3f} GiB/s.", p_transfer.getLabel(), transferSpeed);
  size_t invalidRunCount =
      std::count_if(errorCounts.begin(), errorCounts.end(), [](uint32_t p_errorCount) { return p_errorCount != 0; });
  GBB_WARN_UNLESS(invalidRunCount == 0, "{} of {} runs reported data corruption.", invalidRunCount, runCount);
  return {.label = p_transfer.getLabel(),
          .apiName = p_transfer.getApiName(),
          .srcDevIdx = p_transfer.getSrcDevIdx(),
          .dstDevIdx = p_transfer.getDstDevIdx(),
          .gibPerSecond = transferSpeed};
}
} // namespace gbb