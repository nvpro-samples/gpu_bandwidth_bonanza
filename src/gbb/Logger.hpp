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
#include <filesystem>
#include <fmt/format.h>
#include <fstream>

#define GBB_NO_BREAK

namespace gbb {
class Logger {
public:
  enum class Severity { eInfo, eWarning, eError };

  Logger(const std::filesystem::path &p_logFilePath);
  ~Logger();

  void log(const std::string &p_file, int32_t p_line, Severity p_severity, const std::string &p_message);
  void logAndThrow(const std::string &p_file, int32_t p_line, const std::string &p_message);

private:
  std::ofstream m_logFile;
};

inline std::unique_ptr<Logger> g_logger;
} // namespace gbb

#define GBB_FORMAT_BLACK(_fmt, ...) fmt::format("\033[30m" _fmt "\033[0m", ##__VA_ARGS__)
#define GBB_RED(_text) fmt::format("\033[31m{}\033[0m", _text)
#define GBB_GREEN(_text) fmt::format("\033[32m{}\033[0m", _text)
#define GBB_YELLOW(_text) fmt::format("\033[33m{}\033[0m", _text)
#define GBB_BLUE(_text) fmt::format("\033[34m{}\033[0m", _text)
#define GBB_MAGENTA(_text) fmt::format("\033[35m{}\033[0m", _text)
#define GBB_FORMAT_CYAN(_fmt, ...) fmt::format("\033[36m" _fmt "\033[0m", ##__VA_ARGS__)

#ifdef GBB_NO_BREAK
#define GBB_DBG_BREAK(_fmt, ...) (void)0
#elif defined(_WIN32)
#define GBB_DBG_BREAK(_fmt, ...)                                                                                       \
  do {                                                                                                                 \
    GBB_ERROR("Breakpoint triggered: " _fmt, ##__VA_ARGS__);                                                           \
    _CrtDbgBreak();                                                                                                    \
  } while (0)
#else
#define GBB_DBG_BREAK(_fmt, ...)                                                                                       \
  do {                                                                                                                 \
    GBB_ERROR(_fmt, ##__VA_ARGS__);                                                                                    \
    raise(SIGTRAP);                                                                                                    \
  } while (0)
#endif

#define GBB_INFO(_fmt, ...)                                                                                            \
  gbb::g_logger->log(__FILE__, __LINE__, gbb::Logger::Severity::eInfo, fmt::format(_fmt, ##__VA_ARGS__))
#define GBB_WARN(_fmt, ...)                                                                                            \
  gbb::g_logger->log(__FILE__, __LINE__, gbb::Logger::Severity::eWarning, fmt::format(_fmt, ##__VA_ARGS__))
#define GBB_ERROR(_fmt, ...)                                                                                           \
  gbb::g_logger->log(__FILE__, __LINE__, gbb::Logger::Severity::eError, fmt::format(_fmt, ##__VA_ARGS__))

#define GBB_INFO_IF(_cond, _fmt, ...)                                                                                  \
  do {                                                                                                                 \
    if (_cond) {                                                                                                       \
      GBB_INFO(_fmt, ##__VA_ARGS__);                                                                                   \
    }                                                                                                                  \
  } while (0)
#define GBB_WARN_IF(_cond, _fmt, ...)                                                                                  \
  do {                                                                                                                 \
    if (_cond) {                                                                                                       \
      GBB_WARN(_fmt, ##__VA_ARGS__);                                                                                   \
    }                                                                                                                  \
  } while (0)
#define GBB_ERROR_IF(_cond, _fmt, ...)                                                                                 \
  do {                                                                                                                 \
    if (_cond) {                                                                                                       \
      GBB_ERROR(_fmt, ##__VA_ARGS__);                                                                                  \
    }                                                                                                                  \
  } while (0)
#define GBB_THROW_IF(_cond, _fmt, ...)                                                                                 \
  do {                                                                                                                 \
    if (_cond) {                                                                                                       \
      GBB_DBG_BREAK(_fmt, ##__VA_ARGS__);                                                                              \
      g_logger->logAndThrow(__FILE__, __LINE__, fmt::format(_fmt, ##__VA_ARGS__));                                     \
      exit(1);                                                                                                         \
    }                                                                                                                  \
  } while (0)

#define GBB_INFO_UNLESS(_cond, _fmt, ...) GBB_INFO_IF(!(_cond), _fmt, ##__VA_ARGS__)
#define GBB_WARN_UNLESS(_cond, _fmt, ...) GBB_WARN_IF(!(_cond), _fmt, ##__VA_ARGS__)
#define GBB_ERROR_UNLESS(_cond, _fmt, ...) GBB_ERROR_IF(!(_cond), _fmt, ##__VA_ARGS__)
#define GBB_THROW_UNLESS(_cond, _fmt, ...) GBB_THROW_IF(!(_cond), _fmt, ##__VA_ARGS__)

#define GBB_THROW(_fmt, ...) GBB_THROW_IF(true, _fmt, ##__VA_ARGS__)
