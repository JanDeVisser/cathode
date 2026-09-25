/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>

#include <config.h>

#include <Util/Logging.h>

#include <Lang/Config.h>

namespace Lang {

using namespace Util;
namespace fs = std::filesystem;

fs::path cathode_dir()
{
    fs::path cathodedir { getenv("CATHODE_DIR") ? getenv("CATHODE_DIR") : CATHODE_APPDIR };
    if (cathodedir.empty()) {
        cathodedir = "/usr/share/cathode";
    }
    auto std_cathode { cathodedir / "share" / "std.cth" };
    if (!fs::exists(std_cathode)) {
        fatal("{} not found", std_cathode.string());
    }
    return cathodedir;
}

}
