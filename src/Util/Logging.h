/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstring>
#include <format>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <utility>

#ifdef assert
#undef assert
#endif

#define ENUMVALUE(V) V,
#define ENUMVALUE2(V, Dummy) V,
#define ENUMVALUE_INT(V, I) V = I,
#define ENUMPAYLOAD(V, Payload) Payload,

namespace Util {

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
    S(None, 0)                  \
    S(Trace, 1)                 \
    S(Info, 2)                  \
    S(Warning, 3)               \
    S(Error, 4)                 \
    S(Fatal, 5)

enum class LogLevel {
#undef S
#define S(level, cardinal) level = cardinal,
    ENUMERATE_LOG_LEVELS(S)
#undef S
};

char const             *LogLevel_name(LogLevel);
std::optional<LogLevel> LogLevel_by_name(std::string_view const &);

class Logger;
struct LogCategory;

template<typename T>
struct LogMessage {
    std::string_view file;
    size_t           line;
    std::string_view function;
    std::string_view category;
    LogLevel         level;
    T const         *message;
};

template<typename T, typename... Args>
std::basic_string<T> format_message(LogMessage<T> const &, Args const &...)
{
    std::unreachable();
}

template<typename... Args>
inline std::basic_string<char> format_message(LogMessage<char> const &msg, Args const &...args)
{
    return std::vformat(msg.message, std::make_format_args(args...));
}

template<typename... Args>
inline std::basic_string<wchar_t> format_message(LogMessage<wchar_t> const &msg, Args const &...args)
{
    return std::vformat(msg.message, std::make_wformat_args(args...));
}

extern std::mutex g_logging_mutex;

template<typename T>
void print_message(std::basic_string<T> const &)
{
}

template<>
inline void print_message(std::string const &msg)
{
    std::cerr << msg << std::endl;
    std::cerr.flush();
}

template<>
inline void print_message(std::wstring const &msg)
{
    std::wcerr << msg << std::endl;
    std::wcerr.flush();
}

class Logger {
public:
    bool trace_on(std::string_view const &category) const
    {
        return !category.empty() && (categories.contains(category) || all_enabled);
    }

    template<typename T>
    bool trace_on(LogMessage<T> const &msg) const
    {
        return trace_on(msg.category);
    }

    template<typename T, typename... Args>
    void logmsg(LogMessage<T> const &msg, Args const &...args)
    {
        std::lock_guard<std::mutex> const lock(g_logging_mutex);
        if (msg.level < level || (msg.level == LogLevel::Trace && !trace_on(msg))) {
            return;
        }
        std::string_view f(msg.file);
        if (f.front() == '/') {
            auto ix = f.find_last_of('/');
            if (ix != std::string_view::npos) {
                auto len = f.length() - ix - 1;
                if (len > 19) {
                    len = 19;
                }
                f = f.substr(ix + 1, len);
            }
        }
        auto file_line = std::format("{}:{}", f, msg.line);
        auto prefix = std::format("{:<24}:{:<20}:{:<5}:", file_line, msg.function, LogLevel_name(msg.level));
        auto message = format_message(msg, args...);
        print_message(message);
    }

    template<typename T, typename... Args>
    void info_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        logmsg<T>({ file, line, function, "", LogLevel::Info, message }, std::forward<Args>(args)...);
        exit(1);
    }

    template<typename T, typename... Args>
    void warning_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        logmsg<T>({ file, line, function, "", LogLevel::Warning, message }, std::forward<Args>(args)...);
        exit(1);
    }

    template<typename T, typename... Args>
    void error_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        logmsg<T>({ file, line, function, "", LogLevel::Error, message }, std::forward<Args>(args)...);
        exit(1);
    }

    template<typename T, typename... Args>
    __attribute__((noreturn)) void fatal_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args const &...args)
    {
        logmsg<T>(LogMessage<T> { file, line, function, "", LogLevel::Fatal, message }, std::forward<Args const &>(args)...);
        abort();
    }

    template<typename T, typename... Args>
    void assert_msg(std::string_view const &file, size_t line, std::string_view const &function, bool condition, T const *message, Args const &...args)
    {
        if (condition)
            return;
        logmsg<T>(LogMessage<T> { file, line, function, "", LogLevel::Fatal, message }, std::forward<Args const &>(args)...);
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

struct LogCategory {
public:
    std::string name {};

    LogCategory(std::string_view name) noexcept;
    LogCategory(LogCategory const &) = default;

    template<typename T, typename... Args>
    void logmsg(LogMessage<T> const &msg, Args &&...args)
    {
        Logger::get_logger().logmsg(msg, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    void trace_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        Logger::get_logger().logmsg<T>({ file, line, function, name, LogLevel::Trace, message }, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    void info_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        Logger::get_logger().logmsg<T>({ file, line, function, name, LogLevel::Info, message }, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    void warning_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        Logger::get_logger().logmsg(LogMessage<char> { file, line, function, name, LogLevel::Warning, message }, std::forward<Args>(args)...);
    }

    template<typename T, typename... Args>
    void error_msg(std::string_view const &file, size_t line, std::string_view const &function, T const *message, Args &&...args)
    {
        Logger::get_logger().logmsg({ file, line, function, name, LogLevel::Error, message }, std::forward<Args>(args)...);
    }

    static std::clock_t start();

    template<typename T, typename... Args>
    void log_duration(std::clock_t clock_start, std::string_view const &file, size_t line, std::string_view const &caller, T const *msg, Args &&...args)
    {
        auto clock_end = std::clock();
        auto duration_ms = (unsigned long) (1000.0 * ((float) clock_end - (float) clock_start) / CLOCKS_PER_SEC);

        auto       msg_with_timing = std::basic_string<T>(msg).append(" {d}.{03d} sec");
        LogMessage log_message {
            file,
            line,
            caller,
            name,
            LogLevel::Trace,
            msg_with_timing,
        };
        logmsg(log_message, std::forward<Args>(args)..., duration_ms / 1000, duration_ms % 1000);
    }
};

#define trace(module, fmt, args...)                                                      \
    do {                                                                                 \
        Util::LogCategory(#module).trace_msg(__FILE__, __LINE__, __func__, fmt, ##args); \
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

#define log_timestamp_start(module) (Util::LogCategory(#module).start())
#define log_timestamp_end(module, ts, fmt, ...)                                                 \
    do {                                                                                        \
        Util::LogCategory(#module).log_duration(ts, __FILE__, __LINE__, __func__, fmt, ##args); \
    } while (true)

#define assert(condition)                                                                                                 \
    do {                                                                                                                  \
        Util::Logger::get_logger().assert_msg(__FILE__, __LINE__, __func__, condition, "Assertion error: {}" #condition); \
    } while (false)

#define assert_with_msg(condition, fmt, args...)                                                                                          \
    do {                                                                                                                                  \
        Util::Logger::get_logger().assert_msg(__FILE__, __LINE__, __func__, condition, L"Assertion error: " #condition ": " fmt, ##args); \
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
