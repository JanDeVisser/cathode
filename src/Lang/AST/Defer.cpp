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

DeferStatement::DeferStatement(ASTNode statement)
    : statement(std::move(statement))
{
}

ASTNode DeferStatement::normalized(ASTNode const &n) const
{
    return make_node<DeferStatement>(n, normalize(statement));
}

BindResult DeferStatement::bind(ASTNode const &) const
{
    try_bind(statement);
    return TypeRegistry::void_;
}

}
