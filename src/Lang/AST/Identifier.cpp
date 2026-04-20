/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

#include <Lang/QBE/QBE.h>

namespace Lang {

using namespace std::literals;

Identifier::Identifier(std::wstring_view const identifier)
    : identifier(identifier)
{
}

BindResult Identifier::bind(ASTNode const &n) const
{
    Parser     &parser { *(n.repo) };
    auto const &type = parser.namespaces.back()->type_of(identifier);
    if (type == nullptr) {
        if (parser.pass == 0) {
            return BindError { ASTStatus::Undetermined };
        } else {
            return n.bind_error(L"Unresolved identifier `{}`", identifier);
        }
    }
    if (is<ModuleType>(type)) {
        auto proxy { parser.namespaces.back()->find_module(identifier) };
        auto ret = make_node<ModuleProxy>(n, std::wstring { identifier }, get<ModuleProxy>(proxy).module);
        ret->bound_type = type;
        ret->status = ASTStatus::Bound;
    }
    auto var { parser.namespaces.back()->find_variable(identifier) };
    if (var != nullptr && is_constant(var) && is<VariableDeclaration>(var)) {
        n->superceded_by = get<VariableDeclaration>(var).initializer;
    }
    return type;
}

StampedIdentifier::StampedIdentifier(std::wstring_view const identifier, ASTNodes arguments)
    : Identifier(identifier)
    , arguments(std::move(arguments))
{
}

VariableDeclaration::VariableDeclaration(std::wstring name, ASTNode type_name, ASTNode initializer, bool is_const)
    : name(std::move(name))
    , type_name(type_name)
    , initializer(std::move(initializer))
    , is_const(is_const)
{
}

ASTNode VariableDeclaration::normalized(ASTNode const &n) const
{
    normalize(type_name);
    normalize(initializer);
    return n;
}

BindResult VariableDeclaration::bind(ASTNode const &n) const
{
    auto  my_type = (type_name != nullptr) ? try_bind(type_name) : nullptr;
    auto  init_type = (initializer != nullptr) ? try_bind(initializer) : nullptr;
    auto &parser = *(n.repo);

    assert(my_type != nullptr || init_type != nullptr);

    if (my_type == nullptr) {
        my_type = init_type;
    } else {
        my_type = get<TypeType>(my_type).type;
    }
    if (init_type != nullptr && !init_type->compatible(my_type) && !init_type->assignable_to(my_type)) {
        return n.bind_error(
            L"Type mismatch between declared type `{}` of `{}` and type of initializer value `{}`",
            my_type->name, name, init_type->name);
    }
    if (n.repo->has_variable({ name })) {
        return n.bind_error(L"Duplicate variable name `{}`", name);
    }
    parser.register_variable(name, n);
    return my_type;
}

}
