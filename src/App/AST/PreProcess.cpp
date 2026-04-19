/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/QBE/QBE.h>
#include <string>

namespace Lia {

using namespace std::literals;

Comptime::Comptime(std::wstring_view script_text, ASTNode const &block, std::wstring_view output)
    : script_text(script_text)
    , statements(std::move(block))
    , output(output)
{
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
                auto new_node = make_node<Comptime>(n, script_text, statements, output_string);
                new_node->status = ASTStatus::Normalized;
                return Lia::bind(new_node);
            }
        }
    }

    if (auto parsed_output = parse<Block>(*(n.repo), output); parsed_output) {
        trace("@comptime after parsing");
        if (trace_on()) {
            dump(parsed_output, std::wcerr);
        }
        auto new_node { make_node<Comptime>(n, script_text, normalize(parsed_output), output) };
        trace("@comptime after normalizing");
        auto new_comptime { get<Comptime>(new_node) };
        if (trace_on()) {
            dump(new_comptime.statements, std::wcerr);
        }
        return Lia::bind(new_comptime.statements);
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

Extern::Extern(ASTNodes declarations, std::wstring library)
    : declarations(std::move(declarations))
    , library(std::move(library))
{
}

BindResult Extern::bind(ASTNode const &) const
{
    for (auto const &func : declarations) {
        try_bind(func);
    }
    return TypeRegistry::void_;
}

Import::Import(Strings file_name)
    : file_name(std::move(file_name))
{
}

Include::Include(std::wstring_view file_name)
    : file_name(file_name)
{
}

}
