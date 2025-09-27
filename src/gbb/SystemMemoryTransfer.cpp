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
#include "gbb/SystemMemoryTransfer.hpp"

#include <cstring>
#include <numeric>

namespace gbb {
SystemMemoryTransfer::SystemMemoryTransfer(size_t p_byteSize)
    : Transfer(p_byteSize, DeviceIndex::HOST, DeviceIndex::HOST), m_src(p_byteSize), m_dst(p_byteSize) {
  std::iota(m_src.begin(), m_src.end(), 0);
}

Transfer::Result SystemMemoryTransfer::execute(uint32_t p_seed) {
  auto cpyBeg = std::chrono::high_resolution_clock::now();
  memcpy(m_dst.data(), m_src.data(), this->getByteSize());
  auto cpyEnd = std::chrono::high_resolution_clock::now();
  return {.errorCount = 0, .duration = cpyEnd - cpyBeg};
}
} // namespace gbb
