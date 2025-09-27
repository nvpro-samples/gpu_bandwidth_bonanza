/*
 * Copyright (c) 2024-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

uint32_t rng(uint32_t p_prev) {
  const uint32_t m = 1ull << 31;
  const uint32_t a = 1103515245;
  const uint32_t c = 12345;
  return (a * p_prev + c) % m;
}

struct ShaderInput {
  uint32_t seed;
  uint32_t dataBufferSize;
};

[vk::push_constant]
const ShaderInput g_input;

RWStructuredBuffer<uint32_t> g_data;
RWStructuredBuffer<uint32_t> g_errorCount;

[shader("compute")]
[numthreads(1024, 1, 1)]
void gen(uint3 p_threadId: SV_DispatchThreadID) {
  if (g_input.dataBufferSize <= p_threadId.x) {
    return;
  }
  g_data[p_threadId.x] = rng(g_input.seed + p_threadId.x);
}

[shader("compute")]
[numthreads(1024, 1, 1)]
void check(uint3 p_threadId: SV_DispatchThreadID) {
  if (g_input.dataBufferSize <= p_threadId.x) {
    return;
  }
  if (g_data[p_threadId.x] != rng(g_input.seed + p_threadId.x)) {
    InterlockedAdd(g_errorCount[0], 1);
  }
}
