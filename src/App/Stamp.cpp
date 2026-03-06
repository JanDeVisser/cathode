/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>

namespace Lia {

template<class N>
ASTNode stamp(ASTNode n, N &impl)
{
    return n;
}

template<>
ASTNode stamp(ASTNode n, BinaryExpression &impl)
{
    impl.lhs = stamp(impl.lhs);
    impl.rhs = stamp(impl.rhs);
    return n;
}

template<>
ASTNode stamp(ASTNode n, Block &impl)
{
    ASTNodes stamped;
    for (auto const &stmt : impl.statements) {
        auto new_stmt = stamp(stmt);
        if (new_stmt == nullptr) {
            n.repo->append(n->location, "Stamping statement failed");
            return nullptr;
        }
        stamped.emplace_back(new_stmt);
    }
    impl.statements = stamped;
    return n;
}

template<>
ASTNode stamp(ASTNode n, Call &impl)
{
    impl.callable = stamp(impl.callable);
    impl.arguments = stamp(impl.arguments);
    return n;
}

template<>
ASTNode stamp(ASTNode n, DeferStatement &impl)
{
    impl.statement = stamp(impl.statement);
    return n;
}

template<>
ASTNode stamp(ASTNode n, Enum &impl)
{
    ASTNodes vals {};
    for (auto const &v : impl.values) {
        vals.emplace_back(stamp(v));
    }
    impl.values = vals;
    impl.underlying_type = stamp(impl.underlying_type);
    return n;
}

template<>
ASTNode stamp(ASTNode n, EnumValue &impl)
{
    impl.value = stamp(impl.value);
    impl.payload = stamp(impl.payload);
    return n;
}

template<>
ASTNode stamp(ASTNode n, ExpressionList &impl)
{
    impl.expressions = stamp(impl.expressions);
    return n;
}

template<>
ASTNode stamp(ASTNode n, ForStatement &impl)
{
    impl.range_expr = stamp(impl.range_expr);
    impl.statement = stamp(impl.statement);
    return n;
}

template<>
ASTNode stamp(ASTNode n, FunctionDeclaration &impl)
{
    impl.generics = stamp(impl.generics);
    impl.parameters = stamp(impl.parameters);
    impl.return_type = stamp(impl.return_type);
    return n;
}

template<>
ASTNode stamp(ASTNode n, FunctionDefinition &impl)
{
    impl.declaration = stamp(impl.declaration);
    impl.implementation = stamp(impl.implementation);
    return n;
}

template<>
ASTNode stamp(ASTNode n, IfStatement &impl)
{
    impl.condition = stamp(impl.condition);
    impl.if_branch = stamp(impl.if_branch);
    impl.else_branch = stamp(impl.else_branch);
    return n;
}

template<>
ASTNode stamp(ASTNode n, LoopStatement &impl)
{
    impl.statement = stamp(impl.statement);
    return n;
}

template<>
ASTNode stamp(ASTNode n, Parameter &impl)
{
    impl.type_name = stamp(impl.type_name);
    return n;
}

template<>
ASTNode stamp(ASTNode n, PublicDeclaration &impl)
{
    impl.declaration = stamp(impl.declaration);
    return n;
}

template<>
ASTNode stamp(ASTNode n, Return &impl)
{
    impl.expression = stamp(impl.expression);
    return n;
}

template<>
ASTNode stamp(ASTNode n, StampedIdentifier &impl)
{
    impl.arguments = stamp(impl.arguments);
    return n;
}

template<>
ASTNode stamp(ASTNode n, Struct &impl)
{
    impl.members = stamp(impl.members);
    return n;
}

template<>
ASTNode stamp(ASTNode n, StructMember &impl)
{
    impl.member_type = stamp(impl.member_type);
    return n;
}

template<>
ASTNode stamp(ASTNode n, UnaryExpression &impl)
{
    impl.operand = stamp(impl.operand);
    return n;
}

template<>
ASTNode stamp(ASTNode n, VariableDeclaration &impl)
{
    impl.type_name = stamp(impl.type_name);
    impl.initializer = stamp(impl.initializer);
    return n;
}

template<>
ASTNode stamp(ASTNode n, WhileStatement &impl)
{
    impl.condition = stamp(impl.condition);
    impl.statement = stamp(impl.statement);
    return n;
}

template<>
ASTNode stamp(ASTNode n, Yield &impl)
{
    impl.statement = stamp(impl.statement);
    return n;
}

ASTNode stamp(ASTNode n)
{
    if (n == nullptr) {
        return nullptr;
    }
    auto  &parser { *(n.repo) };
    size_t stack_size { parser.namespaces.size() };
    if (n->ns != nullptr) {
        n.repo->push_namespace(n);
    }
    ASTNode ret = std::visit(
        [&n](auto impl) -> ASTNode {
            ASTNode ret = copy_node<decltype(impl)>(n, std::move(impl));
            return stamp<decltype(impl)>(ret, std::get<decltype(impl)>(ret->node));
        },
        n->node);
    while (parser.namespaces.size() > stack_size) {
        n.repo->pop_namespace(n);
    }
    return ret;
}

ASTNodes stamp(ASTNodes nodes)
{
    ASTNodes stamped;
    for (auto n : nodes) {
        stamped.emplace_back(stamp(n));
    }
    return stamped;
}

}
