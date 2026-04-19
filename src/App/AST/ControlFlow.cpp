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

Block::Block(ASTNodes statements, Label label)
    : statements(std::move(statements))
    , label(std::move(label))
{
}

BindResult Block::bind(ASTNode const &) const
{
    auto types = try_bind_nodes(statements);
    if (!types.empty()) {
        return types.back();
    }
    return TypeRegistry::void_;
}

Break::Break(Label label, ASTNode block)
    : label(std::move(label))
    , block(std::move(block))
{
}

BindResult Break::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

Continue::Continue(Label label)
    : label(std::move(label))
{
}

BindResult Continue::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

ForStatement::ForStatement(std::wstring var, ASTNode expr, ASTNode statement, Label label)
    : range_variable(std::move(var))
    , range_expr(std::move(expr))
    , statement(statement)
    , label(std::move(label))
{
    assert(this->range_expr != nullptr);
    assert(this->statement != nullptr);
}

BindResult ForStatement::bind(ASTNode const &n) const
{
    try_bind(range_expr);
    auto range_type = range_expr->bound_type;
    if (!is<RangeType>(range_type) && !(is<TypeType>(range_type) && is<EnumType>(get<TypeType>(range_type).type))) {
        return n.bind_error(L"`for` loop range expression is a `{}`, not a range", range_type->to_string());
    }
    ASTNode variable_node { nullptr };
    if (is<TypeType>(range_type)) {
        auto enum_type { get<TypeType>(range_type).type };
        auto enum_descr { get<EnumType>(enum_type) };
        variable_node = (n.repo)->make_node<TagValue>(n->location, enum_descr.values[0].value, enum_descr.values[0].label, TypeRegistry::void_, nullptr);
        variable_node->bound_type = enum_type;
        variable_node->status = ASTStatus::Bound;
    } else {
        variable_node = get<BinaryExpression>(range_expr).lhs;
    }
    n->ns->register_variable(range_variable, variable_node);
    return try_bind(statement);
}

IfStatement::IfStatement(ASTNode condition, ASTNode if_branch, ASTNode else_branch, Label label)
    : condition(condition)
    , if_branch(if_branch)
    , else_branch(else_branch)
    , label(std::move(label))
{
    assert(condition != nullptr && if_branch != nullptr);
}

BindResult IfStatement::bind(ASTNode const &n) const
{
    try_bind(condition);
    if (!condition->bound_type->assignable_to(TypeRegistry::boolean)) {
        return n.bind_error(
            L"`if` loop condition is a `{}`, not a boolean",
            condition->bound_type->name);
    }
    try_bind(if_branch);
    if (else_branch != nullptr) {
        try_bind(else_branch);
    }
    auto if_type = if_branch->bound_type;
    if (else_branch == nullptr || else_branch->bound_type == if_type) {
        return if_type;
    }
    return BindError { ASTStatus::Ambiguous };
}

LoopStatement::LoopStatement(Label label, ASTNode statement)
    : label(std::move(label))
    , statement(std::move(statement))
{
    assert(this->statement != nullptr);
}

BindResult LoopStatement::bind(ASTNode const &) const
{
    return try_bind(statement);
}

Return::Return(ASTNode expression)
    : expression(std::move(expression))
{
}

BindResult Return::bind(ASTNode const &n) const
{
    try_bind(expression);
    auto &parser { *(n.repo) };
    auto  func = parser.current_function();
    assert(func != nullptr && is<FunctionDefinition>(func));
    auto func_signature = get<FunctionDefinition>(func).declaration->bound_type;
    assert(is<FunctionType>(func_signature));
    auto return_type = get<FunctionType>(func_signature).result;
    if (expression != nullptr && !expression->bound_type->assignable_to(return_type)) {
        return n.bind_error(
            L"`return` expression is a `{}` and is not assignable to a `{}`",
            expression->bound_type->name, return_type->name);
    }
    if (expression == nullptr && return_type != TypeRegistry::void_) {
        return n.bind_error(
            L"`return` requires an expression of type `{}`",
            return_type->name);
    }
    if (expression != nullptr && return_type == TypeRegistry::void_) {
        return n.bind_error(
            L"`return` returns from a function returning `void` and therefore cannot return a value");
    }
    return return_type;
}

WhileStatement::WhileStatement(Label label, ASTNode condition, ASTNode statement)
    : label(std::move(label))
    , condition(std::move(condition))
    , statement(std::move(statement))
{
    assert(this->condition != nullptr && this->statement != nullptr);
}

BindResult WhileStatement::bind(ASTNode const &n) const
{
    try_bind(condition);
    pType t = condition->bound_type;
    if (!is<BoolType>(t)) {
        return n.bind_error(L"`while` loop condition is a `{}`, not a boolean", t->name);
    }
    return try_bind(statement);
}

Yield::Yield(Label label, ASTNode statement)
    : label(std::move(label))
    , statement(std::move(statement))
{
}

BindResult Yield::bind(ASTNode const &) const
{
    return try_bind(statement);
}

}
