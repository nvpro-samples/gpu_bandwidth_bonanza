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
#include <type_traits>
#include <vulkan/vulkan.hpp>

#include <fmt/format.h>
#include <stdint.h>
#include <string>

#include <vulkan/vulkan_core.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace gbb {
#ifdef _WIN32
inline bool operator==(const LUID &p_left, const LUID &p_right) {
  return p_left.LowPart == p_right.LowPart && p_left.HighPart == p_right.HighPart;
}
#endif

template <typename T>
std::string uuidToString(const T p_uuid[vk::UuidSize])
  requires(std::is_same_v<T, char> || std::is_same_v<T, uint8_t>)
{
  static_assert(vk::UuidSize == 16);
  return fmt::format(
      "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", p_uuid[0],
      p_uuid[1], p_uuid[2], p_uuid[3], p_uuid[4], p_uuid[5], p_uuid[6], p_uuid[7], p_uuid[8], p_uuid[9], p_uuid[10],
      p_uuid[11], p_uuid[12], p_uuid[13], p_uuid[14], p_uuid[15]);
}

template <typename T>
std::string luidToString(const T p_luid[vk::LuidSize])
  requires(std::is_same_v<T, char> || std::is_same_v<T, uint8_t>)
{
  static_assert(vk::LuidSize == 8);
  return fmt::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}", p_luid[0], p_luid[1], p_luid[2], p_luid[3],
                     p_luid[4], p_luid[5], p_luid[6], p_luid[7]);
}

#ifdef _WIN32
inline std::string luidToString(const LUID &p_luid) {
  static_assert(sizeof(LUID) == 8);
  return luidToString(reinterpret_cast<const uint8_t *>(&p_luid));
}
#endif
} // namespace gbb