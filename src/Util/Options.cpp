/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstring>
#include <iostream>

#include <Util/Options.h>

namespace Util {

using Option = std::pair<std::string_view, std::string_view>;
using Options = std::vector<Option>;

static Options s_options = {};

void set_option(std::string_view option, std::string_view value)
{
    s_options.emplace_back(option, value);
}

std::ostream &operator<<(std::ostream &os, Options const &options)
{
    for (auto const &opt : options) {
        os << opt.first;
        if (!opt.second.empty()) {
            os << ": " << opt.second << "\n";
        }
    }
    return os;
}

int parse_options(int argc, char const **argv)
{
    auto ix = 1;
    while (ix < argc && strlen(argv[ix]) > 2 && strncmp(argv[ix], "--", 2) == 0) {
        std::string_view option = argv[ix] + 2;
        std::string_view value = "true";
        char const      *equals = strchr(argv[ix] + 2, '=');
        if (equals) {
            option = std::string_view(argv[ix] + 2, equals - argv[ix] - 2);
            value = equals + 1;
        }
        set_option(option, value);
        ++ix;
    }
    // std::cout << s_options;
    return ix;
}

std::optional<std::string_view> get_option(std::string_view option)
{
    for (auto opt : s_options) {
        if (opt.first == option) {
            return { opt.second };
        }
    }
    return {};
}

std::vector<std::string_view> get_option_values(std::string_view option)
{
    std::vector<std::string_view> ret = {};
    for (auto opt : s_options) {
        if (opt.first == option) {
            ret.emplace_back(opt.second);
        }
    }
    return ret;
}

bool has_option(std::string_view option)
{
    return get_option(option).has_value();
}
}
