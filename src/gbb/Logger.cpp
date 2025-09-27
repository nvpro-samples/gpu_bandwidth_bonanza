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
#include "Logger.hpp"

#include "Exception.hpp"

#include <fmt/format.h>
#include <iostream>
#include <regex>

namespace gbb {
Logger::Logger(const std::filesystem::path &p_logFilePath) {
  if (std::filesystem::is_directory(p_logFilePath)) {
    std::cout << fmt::format("{}({}): {} Log file path must not be a directory: {}", __FILE__, __LINE__,
                             GBB_RED("[ERROR]"), p_logFilePath.string())
              << std::endl;
  }
  m_logFile.open(p_logFilePath);
}

Logger::~Logger() {}

void Logger::log(const std::string &p_file, int32_t p_line, Severity p_severity, const std::string &p_message) {
  std::string formatted;
  if (p_severity == Severity::eWarning || p_severity == Severity::eError) {
    formatted += fmt::format("{}({}): ", p_file, p_line);
  }
  switch (p_severity) {
  case Severity::eInfo: formatted += p_message; break;
  case Severity::eWarning: formatted += GBB_YELLOW("[WARN] ") + p_message; break;
  case Severity::eError: formatted += GBB_RED("[ERROR] ") + p_message; break;
  }
  static const std::regex p("\033\\[\\d+m");
  std::cout << formatted;
  m_logFile << std::regex_replace(formatted, p, "");
  if (!p_message.ends_with('\n') && !p_message.ends_with('\r')) {
    std::cout << std::endl;
    m_logFile << std::endl;
  }
}

void Logger::logAndThrow(const std::string &p_file, int32_t p_line, const std::string &p_message) {
  this->log(p_file, p_line, Severity::eError, p_message);
  throw gbb::Exception(p_message);
}
} // namespace gbb