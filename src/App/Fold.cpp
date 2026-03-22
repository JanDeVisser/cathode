/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <string>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>

namespace Lia {

using FoldError = std::unexpected<ASTStatus>;
using FoldResult = std::expected<std::optional<SyntaxNode>, ASTStatus>;

template<typename Func>
static FoldResult evaluate_op(Number const &lhs, Number const &rhs, Func const &func)
{
    if (lhs.value.index() != rhs.value.index()) {
        return {};
    }
    return std::visit(
        [&func](auto lhs_value, auto rhs_value) -> Number {
            return Number { func(lhs_value, rhs_value) };
        },
        lhs.value, rhs.value);
}

template<typename Func>
static FoldResult evaluate_relational_op(Number const &lhs, Number const &rhs, Func const &func)
{
    if (lhs.value.index() != rhs.value.index()) {
        return {};
    }
    return std::visit(
        [&func](auto lhs_value, auto rhs_value) -> BoolConstant {
            return BoolConstant { func(lhs_value, rhs_value) };
        },
        lhs.value, rhs.value);
}

#undef S
#define S(O)                                                                                  \
    template<typename Lhs, typename Rhs>                                                      \
    FoldResult evaluate_##O(ASTNode const &, Lhs const &lhs, ASTNode const &, Rhs const &rhs) \
    {                                                                                         \
        return {};                                                                            \
    }
BinOps(S)
#undef S

    template<>
    FoldResult evaluate_Add(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x + y; });
}

template<>
FoldResult evaluate_Subtract(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x - y; });
}

template<>
FoldResult evaluate_Multiply(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x * y; });
}

template<>
FoldResult evaluate_Divide(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    if (get<uint64_t>(rhs) == 0) {
        fatal("Division by zero");
    }
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x / y; });
}

template<>
FoldResult evaluate_Modulo(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    if (get<uint64_t>(rhs) == 0) {
        fatal("Division by zero");
    }
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x % y; });
}

template<>
FoldResult evaluate_Equals(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x == y; });
}

template<>
FoldResult evaluate_NotEqual(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x != y; });
}

template<>
FoldResult evaluate_Less(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x < y; });
}

template<>
FoldResult evaluate_LessEqual(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x <= y; });
}

template<>
FoldResult evaluate_Greater(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x > y; });
}

template<>
FoldResult evaluate_GreaterEqual(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x >= y; });
}

template<>
FoldResult evaluate_BinaryAnd(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x & y; });
}

template<>
FoldResult evaluate_BinaryOr(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x | y; });
}

template<>
FoldResult evaluate_BinaryXor(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x ^ y; });
}

template<>
FoldResult evaluate_ShiftLeft(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x << y; });
}

template<>
FoldResult evaluate_ShiftRight(ASTNode const &, Number const &lhs, ASTNode const &, Number const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x >> y; });
}

template<>
FoldResult evaluate_LogicalAnd(ASTNode const &, BoolConstant const &lhs, ASTNode const &, BoolConstant const &rhs)
{
    return BoolConstant { lhs.value && rhs.value };
}

template<>
FoldResult evaluate_LogicalOr(ASTNode const &, BoolConstant const &lhs, ASTNode const &, BoolConstant const &rhs)
{
    return BoolConstant { lhs.value || rhs.value };
}

FoldResult evaluate_Idempotent(ASTNode const &, Number const &lhs, ASTNode const &, auto const &)
{
    return lhs;
}

FoldResult evaluate_Negate(ASTNode const &, Number const &lhs, ASTNode const &, auto const &)
{
    return evaluate_op(lhs, Number { (uint32_t) 0 },
        [](auto x, auto) { return -x; });
}

FoldResult evaluate_BinaryInvert(ASTNode const &, Number const &number, ASTNode const &, auto const &)
{
    return evaluate_op(number, Number { (uint32_t) 0 },
        [](auto x, auto) { return ~x; });
}

FoldResult evaluate_LogicalInvert(ASTNode const &, BoolConstant const &b, ASTNode const &, auto const &)
{
    return BoolConstant { !b.value };
}

FoldResult evaluate_Sizeof(ASTNode const &, Number const &number, ASTNode const &, auto const &)
{
    return Number { std::visit(
        [](auto v) -> uint64_t {
            return sizeof(decltype(v));
        },
        number.value) };
}

FoldResult evaluate_Sizeof(ASTNode const &, BoolConstant const &b, ASTNode const &, auto const &)
{
    return Number { static_cast<uint64_t>(1) };
}

FoldResult evaluate_Sizeof(ASTNode const &n, TypeSpecification const &spec, ASTNode const &, auto const &)
{
    if (auto const type_maybe = resolve(n); type_maybe != nullptr) {
        return Number { static_cast<uint64_t>(type_maybe->size_of()) };
    }
    return {};
}

FoldResult evaluate_Cast(ASTNode const &, Number const &number, ASTNode &type_node, TypeSpecification const &)
{
    if (auto const type = resolve(type_node); type != nullptr) {
        return std::visit(
            overloads {
                [&number, &type](IntType const &int_type) -> FoldResult {
                    return std::visit(
                        overloads {
                            [&type, &int_type](std::unsigned_integral auto v) -> FoldResult {
                                if (v > int_type.max_value) {
                                    fatal(L"Cannot convert `{}` to `{}`", v, type->name);
                                }
                                return Number { int_type, v };
                            },
                            [&type, &int_type](std::signed_integral auto v) -> FoldResult {
                                if (v > int_type.max_value || v < int_type.min_value) {
                                    fatal(L"Cannot convert `{}` to `{}`", v, type->name);
                                }
                                return Number { int_type, v };
                            } },
                        number.value);
                },
                [&number, &type](EnumType const &enum_type) -> FoldResult {
                    for (EnumType::Value const &enum_value : enum_type.values) {
                        if (enum_value.value == get<int64_t>(number)) {
                            return TagValue { enum_value.value, enum_value.label, nullptr, nullptr };
                        }
                    }
                    fatal(L"Cannot cast integer `{}` to enum `{}`", get<int64_t>(number), type->name);
                },
                [&type](auto const &) -> FoldResult {
                    return {};
                } },
            type->description);
    }
    return {};
}

FoldResult evaluate_Cast(ASTNode const &tag_node, TagValue const &tag, ASTNode &type_node, TypeSpecification const &)
{
    auto &parser { *(tag_node.repo) };
    if (auto const type = resolve(type_node); type != nullptr) {
        if (is<IntType>(type)) {
            auto const &int_type { get<IntType>(type) };
            if (tag.tag_value < int_type.min_value) {
                return tag_node.bind_error(
                    L"Cannot cast enum value `{}.{}` to `{}` because {} < {}",
                    tag_node->bound_type->name, type->name, tag.label, tag.tag_value, int_type.min_value);
            }
            if (tag.tag_value > int_type.max_value) {
                return tag_node.bind_error(
                    L"Cannot cast enum value `{}.{}` to `{}` because {} > {}",
                    tag_node->bound_type->name, type->name, tag.label, tag.tag_value, int_type.max_value);
            }
            return Number { get<IntType>(type), tag.tag_value };
        }
    }
    return {};
}

template<class Lhs, class Rhs>
FoldResult fold(ASTNode lhs, Lhs const &lhs_impl, Operator oper, ASTNode rhs, Rhs const &rhs_impl)
{
    switch (oper) {
#undef S
#define S(O)          \
    case Operator::O: \
        return evaluate_##O(lhs, lhs_impl, rhs, rhs_impl);
        BinOps(S)
#undef S
            default : break;
    }
    return {};
}

FoldResult fold(ASTNode lhs, Operator oper, ASTNode rhs)
{
    if (lhs == nullptr || rhs == nullptr) {
        UNREACHABLE();
    }
    return std::visit(
        [&lhs, &rhs, &oper](auto const &lhs_impl, auto const &rhs_impl) {
            return fold(lhs, lhs_impl, oper, rhs, rhs_impl);
        },
        lhs->node, rhs->node);
}

template<class N>
FoldResult fold(Operator oper, ASTNode n, N const &operand)
{
    switch (oper) {
#undef S
#define S(O)          \
    case Operator::O: \
        return evaluate_##O(n, operand, ASTNode { nullptr }, Void {});
        BinOps(S)
#undef S
            default : UNREACHABLE();
    }
    return {};
}

FoldResult fold(Operator oper, ASTNode node)
{
    if (node == nullptr) {
        UNREACHABLE();
    }
    return std::visit(
        [&node, &oper](auto impl) {
            return fold(oper, node, impl);
        },
        node->node);
}

template<class N>
FoldResult fold(ASTNode n, N const &impl)
{
    return {};
}

template<>
FoldResult fold(ASTNode n, BinaryExpression const &impl)
{
    if (auto folded { fold(impl.lhs, impl.op, impl.rhs) }; folded) {
        return folded;
    }
    return {};
}

template<>
FoldResult fold(ASTNode n, UnaryExpression const &impl)
{
    if (auto folded { fold(impl.op, impl.operand) }; folded) {
        return folded;
    }
    return {};
}

ASTNode fold(ASTNode node)
{
    if (node == nullptr) {
        return nullptr;
    }
    if (auto folded { std::visit(
            [&node](auto impl) -> FoldResult {
                return fold(node, impl);
            },
            node->node) };
        folded) {
        if (auto unwrapped = *folded; unwrapped) {
            SyntaxNode impl { *unwrapped };
            return add_node(node, impl);
        }
    }
    return nullptr;
}

}
