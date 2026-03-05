/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <variant>

#include <App/Type.h>

namespace Lia {

enum class Position {
    Prefix,
    Infix,
    Postfix,
    Closing,
};

enum class Associativity {
    Left,
    Right,
};

#define BinOps(S)    \
    S(Add)           \
    S(AddressOf)     \
    S(Apply)         \
    S(BinaryAnd)     \
    S(BinaryInvert)  \
    S(BinaryOr)      \
    S(BinaryXor)     \
    S(Call)          \
    S(Cast)          \
    S(Divide)        \
    S(Equals)        \
    S(Greater)       \
    S(GreaterEqual)  \
    S(Idempotent)    \
    S(Length)        \
    S(Less)          \
    S(LessEqual)     \
    S(LogicalAnd)    \
    S(LogicalInvert) \
    S(LogicalOr)     \
    S(MemberAccess)  \
    S(Modulo)        \
    S(Multiply)      \
    S(Negate)        \
    S(NotEqual)      \
    S(Range)         \
    S(Sequence)      \
    S(ShiftLeft)     \
    S(ShiftRight)    \
    S(Sizeof)        \
    S(Subscript)     \
    S(Subtract)      \
    S(Unwrap)        \
    S(UnwrapError)

#define AssignmentOps(S) \
    S(Assign)            \
    S(AssignAnd)         \
    S(AssignDecrement)   \
    S(AssignDivide)      \
    S(AssignIncrement)   \
    S(AssignModulo)      \
    S(AssignMultiply)    \
    S(AssignOr)          \
    S(AssignShiftLeft)   \
    S(AssignShiftRight)  \
    S(AssignXor)

#define Operators(S) AssignmentOps(S) BinOps(S)

enum class Operator : int {
#undef S
#define S(O) O,
    Operators(S)
#undef S
};

enum class PseudoType {
    Self,
    Lhs,
    Rhs,
    Refer,
    Error,
    Boolean,
    Byte,
    Long,
    String,
};

using OperandType = std::variant<TypeKind, PseudoType>;

extern char const *Operator_name(Operator op);

}
