/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <dlfcn.h>
#include <filesystem>
#include <mutex>

#include <Util/Logging.h>
#include <Util/Resolve.h>
#include <Util/StringUtil.h>
#include <utility>

namespace Util {

namespace fs = std::filesystem;

std::mutex g_resolve_mutex;

/* ------------------------------------------------------------------------ */

DLError::DLError(char const *m)
    : message((m) ? m : "")
{
}

DLError::DLError(std::string m)
    : message(std::move(m))
{
}

Resolver::Library::Library(std::string img)
    : m_image(std::move(img))
{
    auto result = open();
    if (result.has_value()) {
        m_handle = result.value();
    } else {
        m_my_result = result.error();
    }
}

Resolver::Library::~Library()
{
    if (m_handle) {
        dlclose(m_handle);
    }
}

DLResult<LibHandle> Resolver::Library::result() const
{
    if (is_valid()) {
        return m_handle;
    }
    return std::unexpected<DLError>(m_my_result);
}

std::string Resolver::Library::to_string()
{
    return (!m_image.empty()) ? m_image : "Main Program Image";
}

fs::path Resolver::Library::platform_image(std::string const &image)
{
    if (image.empty()) {
        return "";
    }
    if (image == "libc") {
        return "";
    }
    fs::path platform_image { image };
#ifdef __APPLE__
    platform_image.replace_extension("dylib");
#else
    platform_image.replace_extension("so");
#endif
    return platform_image;
}

DLResult<LibHandle> Resolver::Library::try_open(fs::path const &dir) const
{
    char const *p { nullptr };
    auto        image = platform_image(m_image);
    std::string path_string;
    if (!image.empty()) {
        fs::path const path { dir / image };
        trace("Attempting to open library '{}'", path.string());
        path_string = path.string();
        p = path_string.c_str();
    } else {
        trace("Attempting to open main program module");
    }
    dlerror();
    if (auto const lib_handle = dlopen(p, RTLD_NOW | RTLD_GLOBAL); lib_handle) {
        dlerror();
        trace("Successfully opened '{}'", (p) ? p : "main program module");
        return LibHandle { lib_handle };
    }
    return std::unexpected<DLError>(std::in_place_t {});
}

DLResult<LibHandle> Resolver::Library::open()
{
    auto image = platform_image(m_image);
    if (!image.empty()) {
        trace("resolve_open('{}') ~ '{}'", m_image, image.string());
    } else {
        trace("resolve_open('Main Program Image')");
    }
    DLResult<LibHandle> ret { std::unexpected<DLError>(std::in_place_t {}) };
    m_handle = nullptr;
    if (!m_image.empty()) {
        fs::path lia_dir { getenv("LIA_DIR") ? getenv("LIA_DIR") : LIA_APPDIR };
        if (lia_dir.empty()) {
            lia_dir = "/usr/share/lia";
        }
        ret = try_open(lia_dir / "lib");
        if (!ret.has_value()) {
            ret = try_open(lia_dir / "bin");
        }
        if (!ret.has_value()) {
            ret = try_open(lia_dir);
        }
        if (!ret.has_value()) {
            ret = try_open(lia_dir / "share/lib");
        }
        if (!ret.has_value()) {
            ret = try_open(fs::path { "lib" });
        }
        if (!ret.has_value()) {
            ret = try_open(fs::path { "bin" });
        }
        if (!ret.has_value()) {
            ret = try_open(fs::path { "share/lib" });
        }
        if (!ret.has_value()) {
            ret = try_open(fs::path { "/" } / "usr" / "lib");
        }
        if (!ret.has_value()) {
            ret = try_open(fs::path { "/" } / "usr" / "lib64");
        }
#ifdef __APPLE__
        if (!ret.has_value()) {
            ret = try_open(fs::path { "/" } / "opt" / "homebrew" / "lib");
        }
#endif
        if (!ret.has_value()) {
            ret = try_open(fs::current_path());
        }
    } else {
        ret = try_open("");
    }
    if (ret.has_value()) {
        m_handle = ret.value();
        if (!image.empty()) {
            auto result = get_function(LIA_INIT);
            if (result.has_value()) {
                if (auto func_ptr = result.value(); func_ptr != nullptr) {
                    trace("resolve_open('{}') Executing initializer", to_string());
                    (func_ptr)();
                } else {
                    trace("resolve_open('{}') No initializer", to_string());
                }
            } else {
                log_error("resolve_open('{}') Error finding initializer: {}",
                    to_string(), result.error().message);
                m_my_result = result.error();
                return std::unexpected<DLError>(result.error());
            }
        }
        trace("Library '{}' opened successfully", to_string());
    } else {
        log_error("Resolver::Library::open('{}') FAILED", to_string());
        m_my_result = ret.error();
    }
    return ret;
}

DLResult<void_t> Resolver::Library::get_function(std::string const &function_name)
{
    if (!m_my_result.message.empty()) {
        return std::unexpected<DLError>(m_my_result);
    }
    if (m_functions.contains(function_name)) {
        return m_functions[function_name];
    }
    trace("dlsym('{}', '{}')", to_string(), function_name);
    if (auto const function = dlsym(m_handle.handle, function_name.c_str()); function != nullptr) {
        auto const func_ptr = reinterpret_cast<void_t>(function);
        m_functions[function_name] = func_ptr;
        return func_ptr;
    }
// 'undefined symbol' is returned with an empty result pointer
#ifdef __APPLE__
    if (std::string_view err { dlerror() }; err.find("symbol not found") == std::string::npos) {
#else
    if (std::string_view err { dlerror() }; err.find("undefined symbol") == std::string::npos) {
#endif
        return std::unexpected<DLError>(std::string { err });
    }
    m_functions[function_name] = nullptr;
    return nullptr;
}

/* ------------------------------------------------------------------------ */

DLResult<LibHandle> Resolver::open(std::string const &image)
{
    auto const platform_image = Library::platform_image(image);
    if (!m_images.contains(platform_image)) {
        if (auto const lib = std::make_shared<Library>(image); lib->is_valid()) {
            m_images[platform_image] = lib;
        } else {
            return lib->result();
        }
    }
    return m_images[platform_image]->result();
}

DLResult<void_t> Resolver::resolve(std::string const &func_name)
{
    std::lock_guard<std::mutex> const lock(g_resolve_mutex);
    auto                              s = func_name;
    if (auto const paren = s.find_first_of('('); paren != std::string::npos) {
        s.erase(paren);
        while (s[s.length() - 1] == ' ')
            s.erase(s.length() - 1);
    }
    // LOLWUT?
    // if (auto const space = s.find_first_of(' '); space != std::string::npos) {
    //     s.erase(0, space);
    //     while (s[0] == ' ')
    //         s.erase(0, 1);
    // }
    while (isblank(s[0])) {
        s.erase(0, 1);
    }

    std::string image;
    std::string function;
    auto        name = split(std::string_view { s }, ':');
    switch (name.size()) {
    case 2:
        image = name.front();
        /* fall through */
    case 1:
        function = name.back();
        break;
    default:
        return std::unexpected<DLError>(std::format("Invalid function reference '{}'", func_name).c_str());
    }

    if (auto result = open(image); !result.has_value()) {
        return std::unexpected<DLError>(result.error());
    }
    auto lib = m_images[Library::platform_image(image)];
    if (auto res = lib->get_function(function); res.has_value()) {
        return res.value();
    } else {
        return std::unexpected<DLError>(res.error());
    }
}

Resolver &Resolver::get_resolver() noexcept
{
    static Resolver                  *resolver = nullptr;
    std::lock_guard<std::mutex> const lock(g_resolve_mutex);
    if (!resolver) {
        resolver = new Resolver();
    }
    return *resolver;
}

}
