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
#include "gbb/Api.hpp"
#include "gbb/Exception.hpp"
#include "gbb/Logger.hpp"
#include "gbb/Options.hpp"
#include "gbb/ResultProcessor.hpp"
#include "gbb/SystemMemoryTransfer.hpp"
#include "gbb/TransferBenchmark.hpp"
#include "gbb/VulkanApi.hpp"

#ifdef GBB_CUDA_ENABLED
#include "gbb/CudaApi.hpp"
#include "gbb/NvmlInterface.hpp"
#endif

#ifdef GBB_D3D12_ENABLED
#include "gbb/D3D12Api.hpp"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <vector>

#ifndef APP_NAME
#define APP_NAME gbb_bandwidth_bonanza
#endif

#if defined(APP_VERSION_MAJOR) && defined(APP_VERSION_MINOR) && defined(APP_VERSION_PATCH)
#if APP_HAS_UNCOMMITTED_CHANGES
#define APP_VERSION APP_VERSION_MAJOR "." APP_VERSION_MINOR "." APP_VERSION_PATCH " (custom build)"
#else
#define APP_VERSION APP_VERSION_MAJOR "." APP_VERSION_MINOR "." APP_VERSION_PATCH
#endif
#else
#define APP_VERSION "uknown"
#endif

#if !defined(APP_COMMIT_HASH) or !defined(APP_HAS_UNCOMMITTED_CHANGES)
#define APP_COMMIT_HASH "?"
#define APP_HAS_UNCOMMITTED_CHANGES 1
#endif

namespace gbb {
void printVersion() {
  GBB_INFO("App name                       │ {}", APP_NAME);
  GBB_INFO("Version                        │ {}", APP_VERSION);
  GBB_INFO("Base git commit hash           │ {}", APP_COMMIT_HASH);
  GBB_INFO("Built with uncommitted changes │ {}", APP_HAS_UNCOMMITTED_CHANGES ? GBB_YELLOW("yes") : "no");
#ifdef GBB_CUDA_ENABLED
  GBB_INFO("Built with CUDA support        │ {}", "yes");
#else
  GBB_INFO("Built with CUDA support        │ {}", GBB_YELLOW("no"));
#endif
#ifdef GBB_D3D12_ENABLED
  GBB_INFO("Built with D3D12 support       │ {}", "yes");
#else
  GBB_INFO("Built with D3D12 support       │ {}", GBB_YELLOW("no"));
#endif
}

void runBenchmarks(ResultProcessor &p_resultProcessor, Api &p_api, std::chrono::milliseconds p_durationPerDirection) {
  std::vector<TransferBenchmark::Result> results;
  results.emplace_back(TransferBenchmark::run(*p_api.createTransfer(256ull << 20, DeviceIndex::HOST, DeviceIndex::HOST),
                                              p_durationPerDirection));
  for (uint32_t src = 0; src < p_api.getPhysicalDeviceCount(); ++src) {
    results.emplace_back(
        TransferBenchmark::run(*p_api.createTransfer(256ull << 20, src, DeviceIndex::HOST), p_durationPerDirection));
    for (uint32_t dst = 0; dst < p_api.getPhysicalDeviceCount(); ++dst) {
      results.emplace_back(
          TransferBenchmark::run(*p_api.createTransfer(256ull << 20, src, dst), p_durationPerDirection));
    }
    results.emplace_back(
        TransferBenchmark::run(*p_api.createTransfer(256ull << 20, DeviceIndex::HOST, src), p_durationPerDirection));
  }
  for (const TransferBenchmark::Result &result : results) {
    p_resultProcessor.pushResult(result);
  }
}

int32_t main(const std::vector<std::string> &p_args) {
  g_logger = std::make_unique<Logger>("./" APP_NAME ".log");

  std::string cudaStatus = GBB_YELLOW("no");
#ifdef GBB_CUDA_ENABLED
  cudaStatus = "yes";
#endif
  std::string d3d12Status = GBB_YELLOW("no");
#ifdef GBB_D3D12_ENABLED
  d3d12Status = "yes";
#endif
  GBB_INFO("{}, version: {}, CUDA: {}, D3D12: {}", APP_NAME, APP_VERSION, cudaStatus, d3d12Status);
  if (std::find(p_args.begin(), p_args.end(), "--version") != p_args.end() ||
      std::find(p_args.begin(), p_args.end(), "-v") != p_args.end()) {
    printVersion();
    return 0;
  }
  if (std::find(p_args.begin(), p_args.end(), "--help") != p_args.end() ||
      std::find(p_args.begin(), p_args.end(), "-h") != p_args.end()) {
    Options::printUsage();
    return 0;
  }
  Options options = Options::fromArgs(p_args);

  std::vector<Api *> apis;

  GBB_WARN_IF(
      options.dxgiAdapterIndex && options.vulkanDeviceGroupIndex,
      "Both, a Vulkan device group index and a DXGI adapter index, were given. DXGI adapter index will be ignored.");

  if (!options.dxgiAdapterIndex && !options.noVulkan) {
    GBB_INFO("\n════╡Vulkan╞════");
    g_vulkan = std::make_unique<VulkanApi>(options.vulkanDeviceGroupIndex.value_or(0), options.printVulkanMemProps);
    apis.emplace_back(g_vulkan.get());
  }
#ifdef GBB_D3D12_ENABLED
  static_assert(sizeof(LUID) == vk::LuidSize);
  if (!options.noD3D12) {
    GBB_INFO("\n════╡D3D12╞════");
    try {
      g_d3d12 = g_vulkan ? std::make_unique<D3D12Api>(reinterpret_cast<const LUID *>(g_vulkan->getLuid()))
                         : std::make_unique<D3D12Api>(options.dxgiAdapterIndex.value_or(0));
      apis.emplace_back(g_d3d12.get());
      if (!options.noVulkan && !g_vulkan) {
        GBB_INFO("\n════╡Vulkan╞════");
        try {
          g_vulkan = std::make_unique<VulkanApi>(reinterpret_cast<const uint8_t *>(&g_d3d12->getLuid()),
                                                 options.printVulkanMemProps);
          apis.emplace_back(g_vulkan.get());
        } catch (const Exception &ex) {
          (void)ex;
          GBB_WARN("Vulkan will be skipped.");
        } catch (const vk::SystemError &ex) {
          (void)ex;
          GBB_WARN("Vulkan will be skipped. {}", ex.what());
        }
      }
    } catch (const Exception &ex) {
      (void)ex;
      GBB_WARN("D3D will be skipped.");
    }
  }
#endif

#ifdef GBB_CUDA_ENABLED
  GBB_INFO("\n════╡CUDA╞════");
  try {
    g_cuda = std::make_unique<CudaApi>();
    if (!options.noCuda) {
      apis.emplace_back(g_cuda.get());
    }
    GBB_INFO("\n════╡NVML╞════");
    try {
      NvmlInterface nvml;
    } catch (const Exception &ex) {
      (void)ex;
      GBB_WARN("NVML report failed: {}", ex.getMessage());
    }
  } catch (const Exception &ex) {
    (void)ex;
    GBB_WARN("CUDA will be skipped.");
  }
#endif

  GBB_INFO("\n════╡Benchmarks╞════");
  ResultProcessor rp;
  for (Api *api : apis) {
    try {
      runBenchmarks(rp, *api, options.durationPerDirection);
    } catch (const Exception &ex) {
      (void)ex;
      GBB_WARN("{} test quit unexpectedly.", api->getName());
    }
  }
  SystemMemoryTransfer sysMemTransfer(256ull << 20);
  rp.pushResult(TransferBenchmark::run(sysMemTransfer, options.durationPerDirection));
  if (!rp.isEmpty()) {
    GBB_INFO("\n════╡Results╞════");
    rp.printDiagram();
    rp.printMatrix();
    if (options.output) {
      rp.writeResults(options.output.value());
    }
  }
  return 0;
}
} // namespace gbb

int32_t main(int32_t p_argc, char *p_argv[]) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  try {
    return gbb::main({p_argv, p_argv + p_argc});
  } catch (...) {
    GBB_ERROR("Application quit unexpectedly.");
    return 1;
  }
}