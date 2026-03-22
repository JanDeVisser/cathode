/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>
#include <iostream>
#include <locale.h>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <Util/IO.h>
#include <Util/Lexer.h>
#include <Util/Logging.h>
#include <Util/Options.h>
#include <Util/StringUtil.h>
#include <Util/TokenLocation.h>
#include <Util/Utf8.h>

#include <App/Config.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/QBE/QBE.h>

namespace Lia {

extern int debugger_main();

using namespace Util;

std::optional<std::wstring> load_file(fs::path const &file)
{
    if (!fs::exists(file)) {
        log_error("File `{}` does not exist", file.string());
        return {};
    }
    auto contents_maybe = read_file_by_name<wchar_t>(file.string());
    if (!contents_maybe.has_value()) {
        log_error("Could not read file `{}`: {}", file.string(), contents_maybe.error().description);
        return {};
    }
    return contents_maybe.value();
};

std::optional<std::wstring> load_file(std::wstring_view fname)
{
    return load_file(fs::path { fname });
}

std::optional<std::wstring> load_directory(fs::path directory)
{
    std::wstring ret;
    for (auto const &entry : fs::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".lia") {
            if (auto contents_maybe = load_file(entry.path()); !contents_maybe) {
                return {};
            } else {
                ret += contents_maybe.value();
            }
        }
    }
    return ret;
}

std::optional<std::wstring> load_files(std::vector<fs::path> const &files)
{
    std::wstring ret;
    for (auto const &file : files) {
        if (fs::is_regular_file(file)) {
            if (auto contents_maybe = load_file(file); !contents_maybe) {
                return {};
            } else {
                ret += contents_maybe.value();
            }
        }
    }
    return ret;
}

struct Builder {
    using Source = std::variant<fs::path, std::vector<fs::path>>;

    fs::path       app_dir;
    Source         source;
    std::string    program_name;
    Parser         parser {};
    QBE::ILProgram program;
    std::wstring   source_text;
    bool           verbose { false };

    bool analyze()
    {
        if (!parse()) {
            log_error("Syntactic parsing failed");
            return false;
        }
        if (has_option("stop-after-parse")) {
            return false;
        }
        if (!normalize() || !bind()) {
            log_error("Semantic analysis failed");
            return false;
        }
        if (auto res = QBE::generate_qbe(parser.program); !res) {
            log_error(L"QBE generation error: {}", res.error());
            return false;
        } else {
            program = std::move(res.value());
        }
        if (has_option("stop-after-analysis")) {
            return false;
        }
        return true;
    }

    bool build()
    {
        if (!analyze()) {
            return false;
        }
        if (!generate_code()) {
            log_error("Code generation failed");
            return false;
        }
        return true;
    }

    int eval()
    {
        if (!analyze()) {
            return false;
        }
        if (has_option("list")) {
            for (auto const &file : program.files) {
                std::wcerr << file.name << ".ssa:\n\n";
                std::wcerr << file << '\n';
            }
        }
        QBE::VM vm { program };
        if (auto res = execute_qbe(vm); !res.has_value()) {
            fatal(L"Evaluation failed: {}", res.error());
        } else {
            return static_cast<int>(res.value());
        }
    }

    int run()
    {
        // FIXME: There should be a way to have Util::Process not mess with
        // stdout and stderr and have it just go to same in this process.
        // Until that's possible we use good old shitty system(3).
        // TODO: Pass command line parameters
        auto cmd = std::format("./{}", program_name);
        info("[CMD] {}", cmd);
        int exit_code = std::system(cmd.c_str());
        if (!WIFEXITED(exit_code)) {
            return -WTERMSIG(exit_code);
        }
        return WEXITSTATUS(exit_code);
    }

    bool parse()
    {
        auto std_lib = load_std_lib();
        if (!std_lib) {
            return false;
        }
        auto program = Lia::parse<Lia::Program>(parser, *std_lib, program_name);
        if (!program) {
            log_error("Error(s) parsing builtins");
            return false;
        }
        if (auto source_text_maybe = std::visit(
                overloads {
                    [this](fs::path const &p) -> std::optional<std::wstring> {
                        if (fs::is_directory(p)) {
                            return load_directory(p);
                        } else if (fs::is_regular_file(p)) {
                            return load_file(p);
                        } else {
                            log_error("File `{}` is not a regular file or directory", p.string());
                            return {};
                        }
                    },
                    [this](std::vector<fs::path> const &paths) -> std::optional<std::wstring> {
                        return load_files(paths);
                    } },
                source);
            !source_text_maybe) {
            return false;
        } else {
            source_text = *source_text_maybe;
            auto mod = Lia::parse<Lia::Module>(parser, source_text, as_utf8(program_name));
            if (!parser.errors.empty()) {
                log_error("Syntax error(s) found:");
                for (auto const &err : parser.errors) {
                    log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
                }
                return false;
            }
            info("Syntactic parsing succeeded", parser.pass);
            if (has_option("dump-trees")) {
                dump(parser.program, std::wcerr);
            }
            return true;
        }
    }

    bool normalize()
    {
        auto normalized = Lia::normalize(parser.program);
        if (!parser.errors.empty()) {
            log_error("Error(s) found during first phase of sematic analysis:");
            for (auto const &err : parser.errors) {
                log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
            }
            return false;
        }
        assert(normalized);
        parser.program = normalized;
        info("First phase of sematic analysis succeeded");
        if (has_option("dump-trees")) {
            dump(parser.program, std::wcerr);
        }
        return true;
    }

    bool bind()
    {
        auto s = parser.bind();
        if (!s.has_value()) {
            info("Second phase of semantic analysis failed after {} pass(es)", parser.pass);
            if (!parser.errors.empty()) {
                log_error("Error(s) found during second phase of semantic analysis:");
                for (auto const &err : parser.errors) {
                    log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
                }
            } else if (parser.program->status != ASTStatus::Undetermined) {
                log_error("Internal error(s) encountered during semantic analysis");
            }
            if (!parser.unbound_nodes.empty()) {
                info("The following nodes could not be bound:");
                for (auto const &n : parser.unbound_nodes) {
                    info(L"{}", n);
                }
            }
            return false;
        }
        info("Second phase of semantic analysis succeeded after {} pass(es)", parser.pass);
        if (has_option("dump-trees")) {
            dump(parser.program, std::wcerr);
        }
        return true;
    }

    bool generate_code()
    {
        if (auto res = QBE::compile_qbe(program); !res) {
            log_error(L"QBE generation error: {}", res.error());
            return false;
        }
        return true;
    }

private:
    Builder()
        : Builder(fs::current_path())
    {
    }

    Builder(fs::path root)
        : app_dir(lia_dir())
        , source(std::move(root))
        , program_name(root.stem().string())
        , verbose(log_config.level == LogLevel::Trace)
    {
    }

    Builder(std::vector<fs::path> source)
        : app_dir(lia_dir())
        , source(std::move(source))
        , program_name(({ auto const &__s = std::get<1>(this->source); assert(!__s.empty()); __s[0].stem().string(); }))
        , verbose(log_config.level == LogLevel::Trace)
    {
    }

    std::optional<std::wstring> load_std_lib()
    {
        return load_file(fs::path { lia_dir() / "share" / "std.lia" });
    }

    template<typename T>
        requires std::is_same_v<T, fs::path> || std::is_same_v<T, std::vector<fs::path>>
    friend std::optional<Builder> make_builder(T);
};

template<typename T>
    requires std::is_same_v<T, fs::path> || std::is_same_v<T, std::vector<fs::path>>
std::optional<Builder> make_builder(T source)
{
    return Builder { source };
}

void usage()
{
    std::cout << "lia - lia language compiler  https://www.lia-lang.org\n\n";
    std::cout << "Usage:\n";
    std::cout << "   lia help | usage | --help, -h - This text\n";
    std::cout << "   lia [OPTIONS] build [-- <arg>...]\n";
    std::cout << "   lia [OPTIONS] compile <file> ... [-- <arg> ...]\n";
    std::cout << "   lia [OPTIONS] eval <file> ... [-- <arg> ...]\n";
    std::cout << "   lia [OPTIONS] debug - Start debugger\n\n";
    std::cout << "   lia version | --version | -v - Display version\n\n";
    std::cout << "Options:\n";
    std::cout << "  --keep-assembly       Do not delete assembly files after compiling\n";
    std::cout << "  --keep-objects        Do not delete object files after linking\n";
    std::cout << "  --list                Write intermediate representation to .lia/<program>.ir\n";
    std::cout << "  --run                 Run the executable build with `build` or `compile`\n";
    std::cout << "                        Pass the command line arguments specified after `--`";
    std::cout << "  --stop-after-analysis Stop after semantic analysis\n";
    std::cout << "  --stop-after-parse    Stop after syntactic parsing\n";
    std::cout << "  --trace               Print debug tracing information\n";
    std::cout << "  --verbose             Provide progress information\n";
    std::cout << "  -Lpath, -llibrary     Passed on to the linker execution\n";
    std::cout << "\n";
    exit(1);
}

void version()
{
    std::print(std::cout, "lia {} {} {}\n",
        LIA_VERSION, LIA_SYSTEM, LIA_SYSTEM_VERSION, LIA_CPU, LIA_COMPILER);
    exit(0);
}

int main(int argc, char const **argv)
{
    setlocale(LC_ALL, "en_US.UTF-8");
    auto arg_ix = parse_options(argc, argv);
    set_logging_config(LoggingConfig { has_option("trace") ? LogLevel::Trace : (has_option("verbose") ? LogLevel::Info : LogLevel::Error) });
    trace("Tracing is ON");
    info("Verbose mode is ON");
    if (arg_ix == argc) {
        if (has_option("version")) {
            version();
        }
        usage();
    }
    if (has_option("version") || strcmp(argv[arg_ix], "usage") == 0 || strcmp(argv[arg_ix], "help") == 0) {
        usage();
    } else if (strcmp(argv[arg_ix], "version") == 0) {
        version();
    } else if (strcmp(argv[arg_ix], "build") == 0) {
        if (auto builder_maybe = make_builder(fs::current_path()); builder_maybe) {
            if (!builder_maybe->build()) {
                return 1;
            }
            if (has_option("run")) {
                return builder_maybe->run();
            }
        }
    } else if (strcmp(argv[arg_ix], "compile") == 0 && argc - arg_ix > 1) {
        std::vector<fs::path> files;
        for (size_t ix = arg_ix + 1; ix < argc; ++ix) {
            files.emplace_back(argv[ix]);
        }
        if (auto builder_maybe = make_builder(files); builder_maybe) {
            if (!builder_maybe->build()) {
                return 1;
            }
            if (has_option("run")) {
                return builder_maybe->run();
            }
        }
    } else if (strcmp(argv[arg_ix], "eval") == 0 && argc - arg_ix > 1) {
        std::vector<fs::path> files;
        for (size_t ix = arg_ix + 1; ix < argc; ++ix) {
            files.emplace_back(argv[ix]);
        }
        if (auto builder_maybe = make_builder(files); builder_maybe) {
            return builder_maybe->eval();
        }
    } else if (strcmp(argv[arg_ix], "debug") == 0 && argc - arg_ix == 1) {
        std::cerr << "Debugger not yet implemented...\n";
        return 1;
    } else {
        usage();
    }
    return 0;
}

}

int main(int argc, char const **argv)
{
    return Lia::main(argc, argv);
}
