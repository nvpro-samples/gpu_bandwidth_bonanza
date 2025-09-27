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
#include <optional>
#include <stdint.h>

namespace gbb {
class DeviceIndex {
public:
  static const DeviceIndex HOST;

  DeviceIndex(uint32_t p_devIdx) : m_devIdx(p_devIdx) {}

  bool isHost() const { return !m_devIdx; }
  uint32_t get() const { return m_devIdx.value(); }
  uint32_t getOrIfHost(uint32_t p_fallbackDevIdx) const { return m_devIdx.value_or(p_fallbackDevIdx); }
  bool operator!=(const DeviceIndex &p_right) const { return m_devIdx != p_right.m_devIdx; }

private:
  std::optional<uint32_t> m_devIdx;

  DeviceIndex() = default;
};

inline const DeviceIndex DeviceIndex::HOST = {};
} // namespace gbb