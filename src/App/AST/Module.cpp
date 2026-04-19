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

namespace Lia {

using namespace std::literals;

ExportDeclaration::ExportDeclaration(std::wstring name, ASTNode declaration)
    : name(std::move(name))
    , declaration(std::move(declaration))
{
    assert(this->declaration != nullptr);
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

BindResult PublicDeclaration::bind(ASTNode const &) const
{
    return bind(declaration);
}

}
