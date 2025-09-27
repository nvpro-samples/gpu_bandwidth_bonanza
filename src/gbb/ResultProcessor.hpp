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

#include <filesystem>
#include <vector>

namespace gbb {
class ResultProcessor {
public:
  bool isEmpty() const { return m_results.empty(); }
  void pushResult(TransferBenchmark::Result p_result);
  void printDiagram() const;
  void printMatrix() const;
  void writeResults(const std::filesystem::path &p_outputFilePath) const;

private:
  typedef std::vector<std::vector<double>> ResultMatrix;

  ResultMatrix buildMatrix(const std::string &p_apiName) const;
  std::string createMatrixString(const ResultMatrix &p_matrix) const;
  double getSymmetryRating(const ResultMatrix &p_matrix) const;

  std::vector<TransferBenchmark::Result> m_results;
  std::vector<std::string> m_apiNames;
};
} // namespace gbb
