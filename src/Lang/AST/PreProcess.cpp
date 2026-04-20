/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>
#include <string>

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <Lang/Config.h>
#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

#include <Lang/QBE/QBE.h>

namespace Lang {

namespace fs = std::filesystem;
using namespace std::literals;

Comptime::Comptime(std::wstring_view script_text, ASTNode const &block, std::wstring_view output)
    : script_text(script_text)
    , statements(std::move(block))
    , output(output)
{
}

ASTNode Comptime::normalized(ASTNode const &n) const
{
    Parser &parser = *(n.repo);
    auto    script = parse<Block>(parser, script_text);

    if (!parser.errors.empty()) {
        log_error("Syntax error(s) found in @comptime block:");
        for (auto const &err : parser.errors) {
            log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
        }
        return n;
    }

    auto synthetic_return_type = parser.make_node<TypeSpecification>(
        n->location,
        TypeNameNode { { L"string" }, ASTNodes { } });
    auto synthetic_decl = parser.make_node<FunctionDeclaration>(
        n->location,
        std::format(L"comptime-{}", *(n.id)),
        ASTNodes { },
        ASTNodes { },
        synthetic_return_type);
    auto synthetic_def = parser.make_node<FunctionDefinition>(
        std::format(L"comptime-{}", *(n.id)),
        synthetic_decl,
        script);
    normalize(synthetic_def);

    script = normalize(synthetic_def);
    trace("@comptime block parsed");
    return make_node<Comptime>(n, script_text, script);
}

BindResult Comptime::bind(ASTNode const &n) const
{
    auto &parser = *(n.repo);
    if (n->bound_type == nullptr) {
        if (auto res = parser.bind(statements); !res) {
            return res;
        } else {
            switch (statements->status) {
            case ASTStatus::InternalError:
                log_error("Internal error(s) encountered during compilation of @comptime block");
                return nullptr;
            case ASTStatus::BindErrors:
            case ASTStatus::Ambiguous: {
                log_error("Error(s) found during compilation of @comptime block:");
                for (auto const &err : parser.errors) {
                    log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
                }
                return parser.bind_error(n->location, L"Bind error in @comptime block");
            }
            case ASTStatus::Undetermined:
                return BindError { ASTStatus::Undetermined };
            case ASTStatus::Initialized:
            case ASTStatus::Normalized:
                UNREACHABLE();
            case ASTStatus::Bound:
                trace(L"Comptime script bind successful");
                break;
            }
            trace("Bound compile time script");
        }
    }

    if (output.empty()) {
        if (auto res = QBE::generate_qbe(statements); !res.has_value()) {
            return parser.bind_error(n->location, res.error());
        } else {
            auto  program = res.value();
            auto &file = program.files[0];
            auto &function = file.functions[0];
            if (trace_on()) {
                trace("Compile time block IR:");
                std::wcerr << file;
                trace("---------------------------------------------------");
            }
            QBE::VM vm { program };
            if (auto exec_res = execute_qbe(vm, file, function, { }); !res.has_value()) {
                return parser.bind_error(n->location, res.error());
            } else {
                auto const output_val = exec_res.value();
                trace("@comptime block executed");
                auto output_string { static_cast<std::wstring>(output_val) };
                trace(L"@comptime output: {}", output_string);
                auto new_node = make_node<Comptime>(n, std::wstring { script_text }, statements, output_string);
                new_node->status = ASTStatus::Normalized;
                return Lang::bind(new_node);
            }
        }
    }

    if (auto parsed_output = parse<Block>(*(n.repo), output); parsed_output) {
        trace("@comptime after parsing");
        if (trace_on()) {
            dump(parsed_output, std::wcerr);
        }
        auto new_node { make_node<Comptime>(n, std::wstring { script_text }, normalize(parsed_output), std::wstring { output }) };
        trace("@comptime after normalizing");
        auto new_comptime { get<Comptime>(new_node) };
        if (trace_on()) {
            dump(new_comptime.statements, std::wcerr);
        }
        return Lang::bind(new_comptime.statements);
    } else {
        log_error("@comptime parse failed");
        for (auto const &err : parser.errors) {
            log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
        }
        return n.bind_error(L"Error(s) parsing result of @comptime block");
    }
    return nullptr;
}

Embed::Embed(std::wstring_view file_name)
    : file_name(file_name)
{
}

ASTNode Embed::normalized(ASTNode const &n) const
{
    auto fname = as_utf8(file_name);
    if (auto contents_maybe = read_file_by_name<wchar_t>(fname); contents_maybe.has_value()) {
        info(L"Embedding `{}`", file_name);
        auto const &contents = contents_maybe.value();
        return normalize(make_node<QuotedString>(n, contents, QuoteType::DoubleQuote));
    } else {
        n.error("Could not open `{}`: {}", fname, contents_maybe.error().to_string());
        return nullptr;
    }
}

Extern::Extern(ASTNodes declarations, std::wstring library)
    : declarations(std::move(declarations))
    , library(std::move(library))
{
}

ASTNode Extern::normalized(ASTNode const &n) const
{
    Parser  &parser { *n.repo };
    ASTNodes normalized;
    for (auto const &decl : declarations) {
        if (is<FunctionDeclaration>(decl)) {
            auto const name { get<FunctionDeclaration>(decl).name };
            normalized.emplace_back(
                normalize(parser.make_node<FunctionDefinition>(
                    decl->location,
                    std::move(name),
                    decl,
                    parser.make_node<ExternLink>(decl->location, std::format(L"{}:{}", library, name)))));
        } else {
            normalized.emplace_back(normalize(decl));
        }
    }
    return make_node<Extern>(n, normalized, library);
}

BindResult Extern::bind(ASTNode const &) const
{
    for (auto const &func : declarations) {
        try_bind(func);
    }
    return TypeRegistry::void_;
}

Include::Include(std::wstring_view file_name)
    : file_name(file_name)
{
}

ASTNode Include::normalized(ASTNode const &n) const
{
    auto fname = as_utf8(file_name);
    if (auto contents_maybe = read_file_by_name<wchar_t>(fname); contents_maybe.has_value()) {
        info(L"Including `{}`", file_name);
        auto const &contents = contents_maybe.value();
        auto        node = parse<Block>(*(n.repo), std::move(contents), fname);
        if (node) {
            node->location = n->location;
            return normalize(node);
        }
    } else {
        n.error(L"Could not open include file `{}`", file_name);
    }
    return nullptr;
}

}
