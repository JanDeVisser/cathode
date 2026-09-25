/*
 * Copyright (c) 2021, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <Util/Error.h>

namespace Util {

namespace fs = std::filesystem;

constexpr static auto const *CATHODE_DIR = "CATHODE_DIR";
constexpr static auto const *CATHODE_INIT = "_cathode_init";

typedef void (*void_t)();

typedef void *lib_handle_t;

struct LibHandle {
    lib_handle_t handle;

    LibHandle(void *h)
        : handle(h)
    {
    }

    operator void *() const { return handle; }
};

struct DLError {
    std::string message { };
    DLError() = default;
    DLError(char const *m);
    DLError(std::string m);
};

template<typename R>
using DLResult = std::expected<R, DLError>;

class Resolver {
public:
    ~Resolver() = default;
    static Resolver    &get_resolver() noexcept;
    DLResult<LibHandle> open(std::string const &);
    DLResult<void_t>    resolve(std::string const &);

private:
    class Library {
    public:
        explicit Library(std::string img);
        ~Library();
        std::string                       to_string();
        static fs::path                   platform_image(std::string const &);
        [[nodiscard]] bool                is_valid() const { return m_my_result.message.empty(); }
        [[nodiscard]] DLResult<LibHandle> result() const;
        DLResult<void_t>                  get_function(std::string const &);

    private:
        DLResult<LibHandle>               open();
        [[nodiscard]] DLResult<LibHandle> try_open(fs::path const &) const;

        LibHandle                     m_handle { nullptr };
        std::string                   m_image { };
        DLError                       m_my_result;
        std::map<std::string, void_t> m_functions { };
        friend Resolver;
    };

    Resolver() = default;
    std::map<std::string, std::shared_ptr<Library>> m_images;
};

}
