/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#ifdef assert
#undef assert
#endif

#define ENUMVALUE(V) V,
#define ENUMVALUE2(V, Dummy) V,
#define ENUMVALUE_INT(V, I) V = I,
#define ENUMPAYLOAD(V, Payload) Payload,

namespace Util {

namespace fs = std::filesystem;

template<class... Ts>
struct overloads : Ts... {
    using Ts::operator()...;
};
template<class... Ts>
overloads(Ts...) -> overloads<Ts...>;

template<class T>
concept numeric = std::is_integral_v<T> || std::is_floating_point_v<T>;

template<class T>
concept numeric_or_bool = std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T, bool>;

#define ENUMERATE_LOG_LEVELS(S) \
    S(None, 0, 0)               \
    S(Trace, 1, 0)              \
    S(Info, 2, 92)              \
    S(Warning, 3, 93)           \
    S(Error, 4, 91)             \
    S(Fatal, 5, 31)

enum class LogLevel {
#undef S
#define S(level, cardinal, color) level = cardinal,
    ENUMERATE_LOG_LEVELS(S)
#undef S
};

char const             *LogLevel_name(LogLevel);
std::ostream           &operator<<(std::ostream &os, LogLevel level);
std::optional<LogLevel> LogLevel_by_name(std::string_view const &);

class Logger;
struct LogCategory;

struct LogMessageMeta {
    std::string_view file;
    size_t           line;
    std::string_view function;
    std::string_view category;
    LogLevel         level;

    std::string file_name() const
    {
        fs::path path { file };
        auto     ret = path.stem().string();
        if (ret.length() > 19) {
            return ret.substr(0, 19);
        }
        return ret;
    }
};

inline std::ostream &operator<<(std::ostream &os, LogMessageMeta const &meta)
{
    if (meta.level == LogLevel::Trace) {
        auto f { meta.file_name() };
        os << f << ':'
           << std::left << std::setw(24 - f.length() - 1) << meta.line << ':'
           << std::setw(20) << meta.function << ':'
           << std::setw(5) << LogLevel_name(meta.level) << ':';
    } else {
        os << meta.level;
    }
    return os;
}

extern std::mutex g_logging_mutex;

class Logger {
public:
    bool trace_on(std::string_view const &category) const
    {
        return !category.empty() && (categories.contains(category) || all_enabled);
    }

    bool trace_on(LogMessageMeta const &msg) const
    {
        return trace_on(msg.category);
    }

    template<typename... Args>
    void logmsg(LogMessageMeta const &meta, char const *fmt, Args const &...args)
    {
        std::lock_guard<std::mutex> const lock(g_logging_mutex);
        if (meta.level < level || (meta.level == LogLevel::Trace && !trace_on(meta))) {
            return;
        }
        std::cerr << meta << ' ';
        std::cerr << std::vformat(fmt, std::make_format_args(args...)) << '\n';
    }

    template<typename... Args>
    void logmsg(LogMessageMeta const &meta, wchar_t const *fmt, Args const &...args)
    {
        std::lock_guard<std::mutex> const lock(g_logging_mutex);
        if (meta.level < level || (meta.level == LogLevel::Trace && !trace_on(meta))) {
            return;
        }
        std::cerr << meta << ' ';
        std::wcerr << std::vformat(fmt, std::make_wformat_args(args...)) << '\n';
    }

    template<typename T, typename... Args>
    void logmsg(std::string_view const &file, size_t line, std::string_view const &function, std::string_view const &category, LogLevel level, T const *fmt, Args const &...args)
    {
        logmsg(LogMessageMeta { file, line, function, category, level }, fmt, args...);
    }

    template<typename T, typename... Args>
    void trace_msg(std::string_view const &file, size_t line, std::string_view const &function, std::string_view const &category, T const *fmt, Args const &...args)
    {
        logmsg(file, line, function, category, LogLevel::Trace, fmt, args...);
    }

    template<typename T, typename... Args>
    void info_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *fmt, Args &&...args)
    {
        logmsg(file, line, function, "", LogLevel::Info, fmt, args...);
    }

    template<typename T, typename... Args>
    void warning_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *fmt, Args &&...args)
    {
        logmsg(file, line, function, "", LogLevel::Warning, fmt, args...);
    }

    template<typename T, typename... Args>
    void error_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *fmt, Args &&...args)
    {
        logmsg(file, line, function, "", LogLevel::Error, fmt, args...);
    }

    template<typename T, typename... Args>
    __attribute__((noreturn)) void fatal_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *fmt, Args const &...args)
    {
        logmsg(file, line, function, "", LogLevel::Fatal, fmt, std::forward<Args const &>(args)...);
        abort();
    }

    template<typename T, typename... Args>
    void assert_msg(std::string_view const &file, size_t line, std::string_view const &function, bool condition, T const *fmt, Args const &...args)
    {
        if (condition)
            return;
        logmsg(file, line, function, "", LogLevel::Fatal, fmt, std::forward<Args const &>(args)...);
        abort();
    }

    static Logger &get_logger();

    std::set<std::string_view> categories {};
    LogLevel                   level { LogLevel::Warning };
    std::string                logfile {};
    bool                       all_enabled { false };

private:
    Logger();
};

#define trace(module, fmt, args...)                                                               \
    do {                                                                                          \
        Util::Logger::get_logger().trace_msg(__FILE__, __LINE__, __func__, #module, fmt, ##args); \
    } while (false)

#define trace_on(module) (Util::Logger::get_logger().trace_on(#module))

#define info(fmt, args...)                                                              \
    do {                                                                                \
        Util::Logger::get_logger().info_msg(__FILE__, __LINE__, __func__, fmt, ##args); \
    } while (false)

#define warning(fmt, args...)                                                              \
    do {                                                                                   \
        Util::Logger::get_logger().warning_msg(__FILE__, __LINE__, __func__, fmt, ##args); \
    } while (false)

#define log_error(fmt, args...)                                                          \
    do {                                                                                 \
        Util::Logger::get_logger().error_msg(__FILE__, __LINE__, __func__, fmt, ##args); \
    } while (false)

#define fatal(fmt, args...)                                                              \
    do {                                                                                 \
        Util::Logger::get_logger().fatal_msg(__FILE__, __LINE__, __func__, fmt, ##args); \
    } while (false)

#define assert(condition)                                                                                                 \
    do {                                                                                                                  \
        Util::Logger::get_logger().assert_msg(__FILE__, __LINE__, __func__, condition, "Assertion error: {}" #condition); \
    } while (false)

#define assert_with_msg(condition, fmt, args...)                                                                                         \
    do {                                                                                                                                 \
        Util::Logger::get_logger().assert_msg(__FILE__, __LINE__, __func__, condition, "Assertion error: " #condition ": " fmt, ##args); \
    } while (false)

#define UNREACHABLE()         \
    do {                      \
        fatal("Unreachable"); \
    } while (false)

#define NYI(fmt, ...)                                                  \
    do {                                                               \
        fatal("Not Yet Implemented: " fmt __VA_OPT__(, ) __VA_ARGS__); \
    } while (false)
}
