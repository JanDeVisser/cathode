/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <concepts>
#include <cstdint>

#include <rt/cathode.h>

#include <Util/Logging.h>
#include <Util/StringUtil.h>

#include <Lang/Parser.h>
#include <Lang/QBE/QBE.h>

namespace Lang::QBE {

using namespace Util;

static bool is_zero(QBEValue const &value);

#undef S
#define S(O) static QBEValue evaluate_##O(QBEValue const &lhs, QBEValue const &rhs);
BinOps(S)
#undef S

    static bool is_zero(QBEValue const &value)
{
    return std::visit(
        overloads {
            [](intptr_t const &value) -> bool {
                return value == 0;
            },
            [](double const &value) -> bool {
                return value == 0.0;
            } },
        value.payload);
}

template<typename Func>
static QBEValue evaluate_op(QBEValue const &lhs, QBEValue const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](std::integral auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { func(lhs_value, rhs_value) };
            },
            [&func](std::integral auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { func(lhs_value, rhs_value) };
            },
            [&func](std::floating_point auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { func(lhs_value, rhs_value) };
            },
            [&func](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { func(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> QBEValue {
                fatal("Operator only applicable to numbers, not to `{}` and `{}`",
                    typeid(decltype(lhs_value)).name(),
                    typeid(decltype(rhs_value)).name());
            } },
        lhs.payload, rhs.payload);
}

template<typename Func>
static QBEValue evaluate_relational_op(QBEValue const &lhs, QBEValue const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](std::integral auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [&func](std::integral auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [&func](std::floating_point auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [&func](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [](auto lhs_value, auto rhs_value) -> QBEValue {
                fatal("Operator only applicable to numbers, not to `{}` and `{}`",
                    typeid(decltype(lhs_value)).name(),
                    typeid(decltype(rhs_value)).name());
            } },
        lhs.payload, rhs.payload);
}

template<typename Func>
static QBEValue evaluate_bitwise_op(QBEValue const &lhs, QBEValue const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](std::integral auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { func(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> QBEValue {
                fatal("Operator only applicable to integers");
            } },
        lhs.payload, rhs.payload);
}

template<typename Func>
static QBEValue evaluate_logical_op(QBEValue const &lhs, QBEValue const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](bool lhs_value, bool rhs_value) -> QBEValue {
                return QBEValue { func(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> QBEValue {
                fatal("Operator only applicable to booleans");
            } },
        lhs.payload, rhs.payload);
}

QBEValue evaluate_AddressOf(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot take address of an value");
}

QBEValue evaluate_Call(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot call a value");
}

QBEValue evaluate_Cast(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot cast a value");
}

QBEValue evaluate_Length(QBEValue const &, QBEValue const &)
{
    fatal("Cannot take length of an QBEValue");
}

QBEValue evaluate_MemberAccess(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot access members of a value");
}

QBEValue evaluate_Range(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot build a range value");
}

QBEValue evaluate_Sequence(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot build a sequence value");
}

QBEValue evaluate_Subscript(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot subscript a value");
}

QBEValue evaluate_Add(QBEValue const &lhs, QBEValue const &rhs)
{
    return std::visit(
        overloads {
            [](std::integral auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { lhs_value + rhs_value };
            },
            [](std::integral auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { lhs_value + rhs_value };
            },
            [](std::floating_point auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { lhs_value + rhs_value };
            },
            [](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { lhs_value + rhs_value };
            },
            [](void *lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { static_cast<void *>(static_cast<uint8_t *>(lhs_value) + static_cast<intptr_t>(rhs_value)) };
            },
            [](auto lhs_value, auto rhs_value) -> QBEValue {
                fatal("Operator only applicable to numbers, not to `{}` and `{}`",
                    typeid(decltype(lhs_value)).name(),
                    typeid(decltype(rhs_value)).name());
            } },
        lhs.payload, rhs.payload);
}

QBEValue evaluate_Subtract(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x - y; });
}

QBEValue evaluate_Multiply(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x * y; });
}

QBEValue evaluate_Divide(QBEValue const &lhs, QBEValue const &rhs)
{
    if (is_zero(rhs)) {
        fatal("Division by zero");
    }
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x / y; });
}

QBEValue evaluate_Modulo(QBEValue const &lhs, QBEValue const &rhs)
{
    if (is_zero(rhs)) {
        fatal("Division by zero");
    }
    return std::visit(
        overloads {
            [](std::integral auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { lhs_value % rhs_value };
            },
            [](std::integral auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { fmod(lhs_value, rhs_value) };
            },
            [](std::floating_point auto lhs_value, std::integral auto rhs_value) -> QBEValue {
                return QBEValue { fmod(lhs_value, rhs_value) };
            },
            [](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> QBEValue {
                return QBEValue { fmod(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> QBEValue {
                fatal("Modulo operation only applicable to numbers");
            } },
        lhs.payload, rhs.payload);
}

QBEValue evaluate_Equals(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x == y; });
}

QBEValue evaluate_NotEqual(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x != y; });
}

QBEValue evaluate_Less(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x < y; });
}

QBEValue evaluate_LessEqual(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x <= y; });
}

QBEValue evaluate_Greater(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x > y; });
}

QBEValue evaluate_GreaterEqual(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x >= y; });
}

QBEValue evaluate_BinaryAnd(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x & y; });
}

QBEValue evaluate_BinaryOr(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x | y; });
}

QBEValue evaluate_BinaryXor(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x ^ y; });
}

QBEValue evaluate_ShiftLeft(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x << y; });
}

QBEValue evaluate_ShiftRight(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x >> y; });
}

QBEValue evaluate_LogicalAnd(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_logical_op(lhs, rhs,
        [](auto x, auto y) { return x && y; });
}

QBEValue evaluate_LogicalOr(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_logical_op(lhs, rhs,
        [](auto x, auto y) { return x || y; });
}

QBEValue evaluate_Idempotent(QBEValue const &lhs, QBEValue const &)
{
    return lhs;
}

QBEValue evaluate_Negate(QBEValue const &lhs, QBEValue const &)
{
    return evaluate_op(lhs, QBEValue { (uint32_t) 0 },
        [](auto x, auto) { return -x; });
}

QBEValue evaluate_BinaryInvert(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, QBEValue { (uint32_t) 0 },
        [](auto x, auto) { return ~x; });
}

QBEValue evaluate_LogicalInvert(QBEValue const &lhs, QBEValue const &rhs)
{
    return evaluate_bitwise_op(lhs, QBEValue { false },
        [](auto x, auto) { return !x; });
}

QBEValue evaluate_Sizeof(QBEValue const &lhs, QBEValue const &)
{
    return QBEValue { size_of(lhs.type) };
}

QBEValue evaluate_Unwrap(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot unwrap a value");
}

QBEValue evaluate_UnwrapError(QBEValue const &lhs, QBEValue const &rhs)
{
    fatal("Cannot unwrap an error of a value");
}

QBEValue evaluate(QBEValue const &lhs, Operator op, QBEValue const &rhs)
{
    switch (op) {
#undef S
#define S(O)          \
    case Operator::O: \
        return evaluate_##O(lhs, rhs);
        BinOps(S)
#undef S
            default : UNREACHABLE();
    }
}

QBEValue evaluate(Operator op, QBEValue const &operand)
{
    switch (op) {
#undef S
#define S(O)          \
    case Operator::O: \
        return evaluate_##O(operand, QBEValue { false });
        BinOps(S)
#undef S
            default : UNREACHABLE();
    }
}

}
