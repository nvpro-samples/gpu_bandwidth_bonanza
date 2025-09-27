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
#include "ResultProcessor.hpp"

#include "Logger.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace gbb {
void ResultProcessor::pushResult(TransferBenchmark::Result p_result) {
  m_results.emplace_back(p_result);
  if (std::find(m_apiNames.begin(), m_apiNames.end(), p_result.apiName) == m_apiNames.end()) {
    m_apiNames.emplace_back(p_result.apiName);
  }
}

void ResultProcessor::printDiagram() const {
  const size_t labelLength = 3;
  std::map<std::string, std::map<std::string, double>> filtered;
  std::set<std::string> apis;
  double maxTransferSpeed = 0.0f;
  for (const TransferBenchmark::Result &result : m_results) {
    if ((!result.srcDevIdx.isHost() || !result.dstDevIdx.isHost()) && result.srcDevIdx != result.dstDevIdx) {
      std::string src = result.srcDevIdx.isHost() ? "h" : std::to_string(result.srcDevIdx.get());
      std::string dst = result.dstDevIdx.isHost() ? "h" : std::to_string(result.dstDevIdx.get());
      filtered[src + ">" + dst][result.apiName] = result.gibPerSecond;
      apis.insert(result.apiName);
      maxTransferSpeed = std::max(maxTransferSpeed, result.gibPerSecond);
    }
  }
  if (filtered.empty()) {
    return;
  }
  std::map<std::string, std::function<std::string(const std::string &)>> formatters = {
      {"Vulkan", [](const std::string &p_text) { return GBB_RED(p_text); }},
      {"CUDA", [](const std::string &p_text) { return GBB_GREEN(p_text); }},
      {"D3D12", [](const std::string &p_text) { return GBB_BLUE(p_text); }}};
  const std::vector<std::string> symbols = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};

  const uint32_t height = 10;
  uint32_t dv = static_cast<uint32_t>(std::ceil(maxTransferSpeed / static_cast<double>(height)));
  uint32_t ceil = dv * height;
  uint32_t yLabelWidth =
      static_cast<uint32_t>(std::ceil(std::log10(static_cast<double>(ceil)) + std::numeric_limits<double>::epsilon()));
  for (uint32_t y = 0; y < height; ++y) {
    bool printYLabel = y % 2 == 0;
    uint32_t v = ceil - y * dv;
    std::cout << (printYLabel ? fmt::format("{:{}d} GiB/s▔▏", v, yLabelWidth)
                              : std::string(yLabelWidth + 7, ' ') + "▏");
    for (const auto &[label, values] : filtered) {
      std::string entry = "";
      for (const auto &[api, value] : values) {
        auto idx = static_cast<uint32_t>(
            (symbols.size() - 1) *
                std::clamp((value - static_cast<double>(v - dv)) / static_cast<double>(dv), 0.0, 1.0) +
            0.5f);
        entry += formatters[api](symbols[idx]);
      }
      size_t spaceCount = std::max(labelLength, values.size()) - std::min(labelLength, values.size()) + 1;
      std::cout << std::string(spaceCount / 2, ' ') << entry << std::string((spaceCount + 1) / 2, ' ');
    }
    std::cout << std::endl;
  }
  std::cout << std::string(yLabelWidth + 8, ' ');
  for (const auto &[label, values] : filtered) {
    std::cout << label << " ";
  }
  std::cout << std::endl;
  std::string legendTop = "╔";
  std::string legend = "║";
  std::string legendBottom = "╚";
  size_t i = 0;
  for (std::string api : apis) {
    for (size_t i = 0; i < 4 + api.length() + 1; ++i) {
      legendTop += "═";
      legendBottom += "═";
    }
    ++i;
    legendTop += i == apis.size() ? "╗" : "╦";
    legendBottom += i == apis.size() ? "╝" : "╩";
    legend += formatters[api](" ▬▬ ") + api + " ║";
  }
  std::cout << legendTop << std::endl;
  std::cout << legend << std::endl;
  std::cout << legendBottom << std::endl;
}

void ResultProcessor::printMatrix() const {
  for (const std::string &apiName : m_apiNames) {
    ResultMatrix matrix = this->buildMatrix(apiName);
    if (!matrix.empty() && !matrix.front().empty()) {
      GBB_INFO("{} transfer speed results\n{}", apiName, this->createMatrixString(matrix));
      if (matrix.size() == matrix.front().size() && matrix.size() != 1) {
        GBB_INFO("{} transfer speed symmetry rating (lower is better): {:.2f}", apiName,
                 this->getSymmetryRating(matrix));
      }
    }
  }
}

std::string ResultProcessor::createMatrixString(const ResultMatrix &p_matrix) const {
  std::stringstream out;
  out << "┌─────";
  for (size_t c = 0; c < p_matrix.front().size(); ++c) {
    out << "┬───────────";
  }
  out << "┐" << std::endl;
  out << "│ ┏━▶ ";
  for (size_t c = 0; c < p_matrix.front().size(); ++c) {
    out << fmt::format("│ {:^10}", c == 0 ? "host" : "dev " + std::to_string(c - 1));
  }
  out << "│" << std::endl;
  for (size_t r = 0; r < p_matrix.size(); ++r) {
    out << "├─────";
    for (size_t c = 0; c < p_matrix.front().size(); ++c) {
      out << "┼───────────";
    }
    out << "┤" << std::endl;

    out << "│" << (r == 0 ? "host " : "dev " + std::to_string(r - 1));
    for (size_t c = 0; c < p_matrix[r].size(); ++c) {
      out << fmt::format("│{:5.1f} GiB/s", p_matrix[r][c]);
    }
    out << "│" << std::endl;
  }
  out << "└─────";
  for (size_t c = 0; c < p_matrix.front().size(); ++c) {
    out << "┴───────────";
  }
  out << "┘" << std::endl;
  return out.str();
}

ResultProcessor::ResultMatrix ResultProcessor::buildMatrix(const std::string &p_apiName) const {
  uint32_t columnCount = 0;
  ResultMatrix matrix;
  for (const TransferBenchmark::Result &result : m_results) {
    if (p_apiName == result.apiName) {
      uint32_t row = result.srcDevIdx.getOrIfHost(static_cast<uint32_t>(-1)) + 1;
      uint32_t col = result.dstDevIdx.getOrIfHost(static_cast<uint32_t>(-1)) + 1;
      if (matrix.size() <= row) {
        matrix.resize(row + 1);
      }
      if (matrix[row].size() <= col) {
        matrix[row].resize(col + 1);
      }
      matrix[row][col] = result.gibPerSecond;
      columnCount = std::max(columnCount, col + 1);
    }
  }
  for (auto &row : matrix) {
    row.resize(columnCount);
  }
  return matrix;
}

double ResultProcessor::getSymmetryRating(const ResultMatrix &p_matrix) const {
  double sum = 0.0;
  for (size_t r = 0; r < p_matrix.size(); ++r) {
    for (size_t c = 0; c < p_matrix[r].size(); ++c) {
      sum += (p_matrix[r][c] - p_matrix[c][r]) * (p_matrix[r][c] - p_matrix[c][r]);
    }
  }
  return std::sqrt(sum);
}

void ResultProcessor::writeResults(const std::filesystem::path &p_outputFilePath) const {
  GBB_THROW_UNLESS(!std::filesystem::is_directory(p_outputFilePath), "Given path points to a directory. {}",
                   p_outputFilePath.string());
  std::ofstream out(p_outputFilePath);
  GBB_THROW_UNLESS(out, "Failed to open file for writing. {}", p_outputFilePath.string());
  for (const std::string &apiName : m_apiNames) {
    out << apiName << std::endl;
    ResultMatrix matrix = this->buildMatrix(apiName);
    out << "from\\to";
    for (size_t c = 0; c < matrix.front().size(); ++c) {
      out << "," << (c == 0 ? "host" : "dev " + std::to_string(c - 1));
    }
    out << std::endl;
    for (size_t r = 0; r < matrix.size(); ++r) {
      out << (r == 0 ? "host" : "dev " + std::to_string(r - 1));
      for (size_t c = 0; c < matrix[r].size(); ++c) {
        out << "," << matrix[r][c];
      }
      out << std::endl;
    }
  }
  GBB_INFO("Results written to {}.", p_outputFilePath.string());
}
} // namespace gbb
