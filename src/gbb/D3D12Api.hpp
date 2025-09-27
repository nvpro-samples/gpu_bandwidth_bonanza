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
#include "Api.hpp"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <string>
#include <variant>

#define GBB_THROW_UNLESS_D3D(_cmd)                                                                                     \
  do {                                                                                                                 \
    HRESULT _res = _cmd;                                                                                               \
    GBB_THROW_UNLESS(SUCCEEDED(_res), "{}", gbb::D3D12Api::toString(_res));                                            \
  } while (0)

namespace gbb {
class D3D12Api : public Api {
public:
  static std::string toString(HRESULT p_res);

  D3D12Api(std::variant<uint32_t, const LUID *> p_selectedAdapter);
  ~D3D12Api();

  ID3D12Device4 *dev() const { return m_device.Get(); }
  UINT getNodeMaskAll() const { return (1u << m_device->GetNodeCount()) - 1u; }
  std::string getName() override { return "D3D12"; }
  uint32_t getPhysicalDeviceCount() override { return m_device->GetNodeCount(); }
  std::unique_ptr<Transfer> createTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx,
                                           DeviceIndex p_dstDevIdx) override;
  const LUID &getLuid() const { return m_luid; }

private:
  LUID m_luid;
  Microsoft::WRL::ComPtr<IDXGIAdapter4> m_adapter;
  Microsoft::WRL::ComPtr<ID3D12Device4> m_device;
};

inline std::unique_ptr<D3D12Api> g_d3d12;
} // namespace gbb