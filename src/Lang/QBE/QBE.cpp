/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>

#include <Util/Align.h>
#include <Util/Logging.h>
#include <Util/Options.h>
#include <Util/Pipe.h>
#include <Util/Process.h>
#include <Util/Utf8.h>

#include <Lang/Config.h>
#include <Lang/Operator.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

#include <Lang/Parser.h>
#include <Lang/QBE/Generate/Generate.h>
#include <Lang/QBE/QBE.h>

namespace Lang::QBE {

std::expected<void, std::wstring> compile_qbe(ILProgram const &program)
{
    fs::path dot_cathode { ".cathode" };
    fs::create_directory(dot_cathode);
    std::vector<fs::path> o_files;

    for (auto const &file : program.files) {
        if (file.has_exports) {
            fs::path file_path = dot_cathode / file.name;
            file_path.replace_extension("ssa");
            {
                fs::path file_path = dot_cathode / file.name;
                file_path.replace_extension("ssa");
                std::wofstream os { file_path.string() };
                os << file;
            }
            auto s_file { file_path };
            s_file.replace_extension("s");
            auto o_file { file_path };
            o_file.replace_extension("o");

            info("[QBE] Compiling `{}`", file_path.string());
            Util::Process qbe { "qbe", "-o", s_file.string(), file_path.string() };
            if (auto res = qbe.execute(); !res.has_value()) {
                return std::unexpected(std::format(L"qbe execution failed: {}", as_wstring(res.error().description)));
            } else if (res.value() != 0) {
                return std::unexpected(std::format(L"qbe failed: {}", as_wstring(qbe.stderr())));
            }
            Util::Process as { "as", "-o", o_file.string(), s_file.string() };
            if (auto res = as.execute(); !res.has_value()) {
                return std::unexpected(std::format(L"as execution failed: {}", as_wstring(res.error().description)));
            } else if (res.value() != 0) {
                return std::unexpected(std::format(L"as failed: {}", as_wstring(as.stderr())));
            }
            if (!has_option("keep-assembly")) {
                fs::remove(file_path);
                fs::remove(s_file);
            }
            o_files.push_back(o_file);
        }
    }

    if (!o_files.empty()) {
        fs::path program_path { as_utf8(program.name) };
        program_path.replace_extension();
        info("[QBE] Linking `{}`", program_path.string());

        std::vector<std::string> ld_args {
            "-o",
            fs::path { as_utf8(program.name) }.replace_extension("").string(),
            // "-no-pie",
            std::format("-L{}/lib", Lang::cathode_dir().string()),
            std::format("-Wl,-rpath,{}/lib", Lang::cathode_dir().string()),
        };
        for (auto const &o : o_files) {
            ld_args.push_back(o.string());
        }
        for (auto const &lib : program.libraries) {
            auto l { (lib.starts_with(L"lib")) ? lib.substr(3) : lib };
            ld_args.emplace_back(std::format("-l{}", as_utf8(l)));
        }
        if (has_option("L")) {
            for (auto const &lib_path : get_option_values("L")) {
                ld_args.push_back(std::format("-L{}", lib_path));
            }
        }
        if (has_option("l")) {
            for (auto const &lib : get_option_values("l")) {
                ld_args.push_back(std::format("-l{}", lib));
            }
        }
        ld_args.append_range(std::array<std::string, 4> { "-lcathodert", "-lm", "-lpthread", "-ldl" });

        Util::Process link { "cc", ld_args };
        if (auto res = link.execute(); !res.has_value()) {
            return std::unexpected(std::format(L"Linker execution failed: {}", as_wstring(res.error().description)));
        } else if (res.value() != 0) {
            return std::unexpected(std::format(L"Linking failed: {}", as_wstring(link.stderr())));
        }
        if (!has_option("keep-objects")) {
            for (auto const &o : o_files) {
                fs::remove(o);
            }
        }
    }
    return { };
}
}
