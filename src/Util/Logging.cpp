/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <iomanip>
#include <mutex>
#include <ostream>
#include <sstream>
#include <utility>

#include <Util/Logging.h>
#include <Util/Options.h>

namespace Util {

std::mutex g_logging_mutex;

char const *LogLevel_name(LogLevel level)
{
    switch (level) {
#undef S
#define S(lvl, cardinal, color) \
    case LogLevel::lvl:         \
        return #lvl;
        ENUMERATE_LOG_LEVELS(S)
#undef S
    default:
        std::unreachable();
    }
}

std::ostream &operator<<(std::ostream &os, LogLevel level)
{
    os << '[';
    switch (level) {
#undef S
#define S(lvl, cardinal, color)                  \
    case LogLevel::lvl:                          \
        if constexpr (color != 0) {              \
            os << "\x1b[" #color "m";            \
        }                                        \
        os << std::left << std::setw(7) << #lvl; \
        if constexpr (color != 0) {              \
            os << "\x1b[m";                      \
        }                                        \
        break;
        ENUMERATE_LOG_LEVELS(S)
#undef S
    default:
        std::unreachable();
    }
    return os << ']';
}

std::optional<LogLevel> LogLevel_by_name(std::string_view const &name)
{
#undef S
#define S(level, cardinal, color) \
    if (name == #level) {         \
        return LogLevel::level;   \
    }
    ENUMERATE_LOG_LEVELS(S)
#undef S
    return {};
}

std::ostream &operator<<(std::ostream &os, Logger const &logger)
{
    // std::set<std::string_view> categories {};
    // LogLevel                   level { LogLevel::Warning };
    // std::string                logfile {};
    // bool                       all_enabled { false };
    os << "Level: " << logger.level << "\n";
    if (logger.level == LogLevel::Trace) {
        if (logger.all_enabled) {
            os << "All trace categories enabled\n";
        } else {
            std::stringstream ss;
            for (auto const &cat : logger.categories) {
                ss << cat << " ";
            }
            os << "Categories: " << ss.str() << "\n";
        }
    }
    if (!logger.logfile.empty()) {
        os << "Log file: " << logger.logfile << "\n";
    }
    return os;
}

Logger::Logger()
{
    auto set_level = [this](std::string_view level_str) {
        auto lvl = LogLevel_by_name(level_str);
        if (lvl.has_value()) {
            level = lvl.value();
        }
    };
    if (auto loglevel = get_option("loglevel"); loglevel.has_value()) {
        set_level(loglevel.value());
    } else if (auto env_level = getenv("LOGLEVEL"); env_level && *env_level) {
        set_level(env_level);
    }

    if (auto logfile = get_option("logfile"); logfile.has_value()) {
        logfile = logfile.value();
    } else if (auto env_logfile = getenv("LOGFILE"); env_logfile) {
        logfile = env_logfile;
    }

    auto add_category = [this](std::string_view const &category) {
        level = LogLevel::Trace;
        if (category == "all") {
            all_enabled = true;
        } else {
            categories.insert(category);
        }
    };

    auto add_categories = [add_category](std::string_view const &categories) {
        auto prev = 0ul;
        for (auto sep = categories.find_first_of(";,", prev); sep != std::string_view::npos; prev = sep, sep = categories.find_first_of(";,", prev + 1)) {
            add_category(categories.substr(prev, sep - prev));
        }
        if (prev < categories.length()) {
            add_category(categories.substr(prev));
        }
    };

    auto const categories = get_option_values("trace");
    for (auto cat : categories) {
        if (cat == "true") {
            cat = "all";
        }
        add_categories(cat);
    }
    if (auto env_trace = getenv("TRACE"); env_trace != nullptr) {
        add_categories(env_trace);
    }
    // std::cout << *this;
}

Logger &Logger::get_logger()
{
    static Logger                    *logger = nullptr;
    std::lock_guard<std::mutex> const lock(g_logging_mutex);
    if (!logger) {
        logger = new Logger();
    }
    return *logger;
}
}
