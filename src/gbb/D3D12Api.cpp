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
#include "D3D12Api.hpp"

#include "D3D12Transfer.hpp"
#include "Logger.hpp"
#include "UuidUtils.hpp"

using Microsoft::WRL::ComPtr;

namespace gbb {
std::string wcs2mbs(const wchar_t *p_source) {
  std::string dst = "";
  std::vector<char> buffer(MB_CUR_MAX);
  while (*p_source) {
    int32_t converted = 0;
    if (wctomb_s(&converted, buffer.data(), buffer.size(), *p_source) == 0) {
      dst.insert(dst.end(), buffer.begin(), buffer.begin() + converted);
    } else {
      dst.push_back('?');
    }
    ++p_source;
  }
  return dst;
}

D3D12Api::D3D12Api(std::variant<uint32_t, const LUID *> p_selectedAdapter) {
  ComPtr<IDXGIFactory7> factory;
  GBB_THROW_UNLESS_D3D(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
  ComPtr<IDXGIAdapter1> tmpAdapter;
  for (UINT i = 0;; ++i) {
    HRESULT r = factory->EnumAdapters1(i, tmpAdapter.ReleaseAndGetAddressOf());
    if (r == DXGI_ERROR_NOT_FOUND) {
      break;
    }
    GBB_THROW_UNLESS_D3D(r);
    ComPtr<IDXGIAdapter4> adapter;
    tmpAdapter.As<IDXGIAdapter4>(&adapter);
    DXGI_ADAPTER_DESC3 adapterDesc = {};
    GBB_THROW_UNLESS_D3D(adapter->GetDesc3(&adapterDesc));
    ComPtr<ID3D12Device4> device;
    GBB_THROW_UNLESS_D3D(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
    bool luidMatches = std::holds_alternative<const LUID *>(p_selectedAdapter) &&
                       *std::get<const LUID *>(p_selectedAdapter) == adapterDesc.AdapterLuid;
    bool indexMatches =
        std::holds_alternative<uint32_t>(p_selectedAdapter) && std::get<uint32_t>(p_selectedAdapter) == i;
    bool selected = luidMatches || indexMatches;
    if (selected) {
      m_luid = adapterDesc.AdapterLuid;
      m_adapter = adapter;
      m_device = device;
    }
    GBB_INFO("DXGI adapter {}: {}, {} node{}, luid [{}]{}", i, wcs2mbs(adapterDesc.Description), device->GetNodeCount(),
             device->GetNodeCount() == 1 ? "" : "s", luidToString(adapterDesc.AdapterLuid),
             selected ? GBB_FORMAT_CYAN(", selected") : "");
  }
  GBB_THROW_UNLESS(m_adapter && m_device, "No matching adapter found.");
}

D3D12Api::~D3D12Api() {}

std::string D3D12Api::toString(HRESULT p_result) {
  char message[256];
  DWORD c = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, p_result, LANG_SYSTEM_DEFAULT, message,
                          std::size(message), nullptr);
  return c == 0 ? fmt::format("Unknown error: {}", p_result) : std::string(message, message + c);
}

std::unique_ptr<Transfer> D3D12Api::createTransfer(size_t p_byteSize, DeviceIndex p_srcDevIdx,
                                                   DeviceIndex p_dstDevIdx) {
  return std::make_unique<D3D12Transfer>(p_byteSize, p_srcDevIdx, p_dstDevIdx);
}
} // namespace gbb