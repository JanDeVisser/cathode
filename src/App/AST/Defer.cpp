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

DeferStatement::DeferStatement(ASTNode statement)
    : statement(std::move(statement))
{
}

BindResult DeferStatement::bind(ASTNode const &) const
{
    try_bind(statement);
    return TypeRegistry::void_;
}

}
