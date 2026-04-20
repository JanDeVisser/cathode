/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <filesystem>

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

ExportDeclaration::ExportDeclaration(std::wstring name, ASTNode declaration)
    : name(std::move(name))
    , declaration(std::move(declaration))
{
    assert(this->declaration != nullptr);
}

ASTNode ExportDeclaration::normalized(ASTNode const &) const
{
    auto normalized { normalize(declaration) };
    std::visit(
        overloads {
            [](FunctionDefinition &impl) {
                impl.visibility = Visibility::Export;
            },
            [](VariableDeclaration &impl) {
                impl.visibility = Visibility::Export;
            },
            [](auto &) {
                UNREACHABLE();
            } },
        normalized->node);
    return normalized;
}

Import::Import(Strings file_name)
    : file_name(std::move(file_name))
{
}

ASTNode Import::normalized(ASTNode const &n) const
{
    assert(!file_name.empty());
    auto     fname { join(file_name, L"/"sv) };
    fs::path path { };
    for (auto const &elem : file_name) {
        path += elem;
    }
    path.concat(".lia");
    if (!fs::exists(path)) {
        path = lia_dir() / "share" / path;
    }
    Parser &parser { *(n.repo) };

    NSNode &ns { parser.namespaces.back() };
    ASTNode proxy;
    for (auto const &elem : file_name) {
        if (proxy = ns->find_module(elem); proxy != nullptr) {
            ns = proxy->ns;
        } else if (!ns->contains(elem)) {
            proxy = parser.make_node<ModuleProxy>(n->location, elem);
            proxy->init_namespace();
            parser.pop_namespace(proxy);
            parser.namespaces.back()->register_module(elem, proxy);
            ns = proxy->ns;
        }
    }

    ASTNode module { nullptr };
    for (auto const &[name, m] : parser.modules) {
        if (name == fname) {
            module = m;
            break;
        }
    }

    if (module == nullptr) {
        if (auto contents_maybe = read_file_by_name<wchar_t>(path.string()); contents_maybe.has_value()) {
            info("Importing module `{}`", path.string());
            auto const &contents = contents_maybe.value();
            module = parse<Module>(parser, std::move(contents), path.string());
            get<ModuleProxy>(proxy).module = module;
        } else {
            n.error(L"Could not open import `{}`", join(file_name, L"."sv));
        }
    }
    if (module) {
        return proxy;
    }
    return n;
}

Module::Module(std::wstring name, std::wstring source)
    : name(std::move(name))
    , source(std::move(source))
{
}

Module::Module(std::wstring name, std::wstring source, ASTNodes const &statements)
    : name(std::move(name))
    , source(std::move(source))
    , statements(statements)
{
}

ASTNode Module::normalized(ASTNode const &n) const
{
    if (n->ns == nullptr) {
        n->init_namespace();
    }
    return make_node<Module>(n, std::wstring { name }, source, normalize(statements));
}

BindResult Module::bind(ASTNode const &) const
{
    try_bind_nodes(statements);
    return TypeRegistry::void_;
}

ModuleProxy::ModuleProxy(std::wstring name, ASTNode module)
    : name(std::move(name))
    , module(std::move(module))
{
}

BindResult ModuleProxy::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

Program::Program(std::wstring name, std::wstring source)
    : name(std::move(name))
    , source(std::move(source))
{
}

Program::Program(std::wstring name, ASTNodes statements)
    : name(std::move(name))
    , statements(std::move(statements))
{
}

ASTNode Program::normalized(ASTNode const &n) const
{
    Parser &parser = *(n.repo);
    assert(parser.program == n);
    n->init_namespace();
    for (auto const &t : TypeRegistry::the().types) {
        n->ns->register_type(t.name, t.id);
    }
    auto normalized_statements { normalize(statements) };
    auto again { true };
    while (again) {
        again = false;
        for (auto &[name, mod] : parser.modules) {
            auto const normalized = normalize(mod);
            if (normalized != mod) {
                parser.modules[name] = normalized;
                again = true;
            }
        }
    }
    auto ret = make_node<Program>(n, std::wstring { name }, normalized_statements);
    return ret;
}

BindResult Program::bind(ASTNode const &n) const
{
    assert(n != nullptr);
    auto &parser { *(n.repo) };
    pType ret { nullptr };
    if (parser.pass == 0) {
        for (auto &[name, mod] : parser.modules) {
            n->ns->register_variable(name, mod);
        }
    }
    try_bind_nodes(statements);
    try_bind_nodes(parser.modules | std::ranges::views::values);
    return TypeRegistry::void_;
}

PublicDeclaration::PublicDeclaration(std::wstring name, ASTNode declaration)
    : name(std::move(name))
    , declaration(std::move(declaration))
{
    assert(this->declaration != nullptr);
}

ASTNode PublicDeclaration::normalized(ASTNode const &) const
{
    auto normalized { normalize(declaration) };
    std::visit(
        overloads {
            [](FunctionDefinition &impl) {
                impl.visibility = Visibility::Public;
            },
            [](VariableDeclaration &impl) {
                impl.visibility = Visibility::Public;
            },
            [](auto &) {
                UNREACHABLE();
            } },
        normalized->node);
    return normalized;
}

}
