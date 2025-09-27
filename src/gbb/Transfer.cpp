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
#include "Transfer.hpp"

#include <fmt/format.h>

namespace gbb {
Transfer::Transfer(size_t p_byteSize, DeviceIndex p_srcDevIdx, DeviceIndex p_dstDevIdx)
    : m_byteSize(p_byteSize), m_srcDevIdx(p_srcDevIdx), m_dstDevIdx(p_dstDevIdx) {}

std::string Transfer::getLabel() const {
  return fmt::format("{} transfer {:5} -> {:5} | ", this->getApiName(),
                     m_srcDevIdx.isHost() ? "host" : "dev " + std::to_string(m_srcDevIdx.get()),
                     m_dstDevIdx.isHost() ? "host" : "dev " + std::to_string(m_dstDevIdx.get()));
}
} // namespace gbb