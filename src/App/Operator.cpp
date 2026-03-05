/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>

namespace Lia {

std::vector<BinaryOperator> binary_ops {
    { TypeKind::IntType, Operator::Add, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::FloatType, Operator::Add, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::Subtract, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::FloatType, Operator::Subtract, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::Multiply, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::FloatType, Operator::Multiply, PseudoType::Lhs, PseudoType::Lhs },
    { PseudoType::String, Operator::Multiply, TypeKind::IntType, PseudoType::Lhs },
    { TypeKind::IntType, Operator::Divide, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::FloatType, Operator::Divide, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::Modulo, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::FloatType, Operator::Modulo, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::Equals, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::FloatType, Operator::Equals, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::OptionalType, Operator::Equals, TypeKind::VoidType, PseudoType::Boolean },
    { TypeKind::IntType, Operator::NotEqual, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::FloatType, Operator::NotEqual, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::IntType, Operator::Less, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::FloatType, Operator::Less, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::IntType, Operator::LessEqual, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::FloatType, Operator::LessEqual, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::IntType, Operator::Greater, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::FloatType, Operator::Greater, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::IntType, Operator::GreaterEqual, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::FloatType, Operator::GreaterEqual, PseudoType::Lhs, PseudoType::Boolean },
    { TypeKind::IntType, Operator::BinaryAnd, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::BinaryOr, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::BinaryXor, PseudoType::Lhs, PseudoType::Lhs },
    { TypeKind::IntType, Operator::ShiftLeft, PseudoType::Byte, PseudoType::Lhs },
    { TypeKind::IntType, Operator::ShiftRight, PseudoType::Byte, PseudoType::Lhs },
    { PseudoType::Boolean, Operator::LogicalAnd, PseudoType::Boolean, PseudoType::Boolean },
    { PseudoType::Boolean, Operator::LogicalOr, PseudoType::Boolean, PseudoType::Boolean },
    { TypeKind::OptionalType, Operator::LogicalOr, PseudoType::Refer, PseudoType::Refer },
    { TypeKind::ResultType, Operator::LogicalOr, PseudoType::Refer, PseudoType::Refer },
    { TypeKind::TaggedUnionType, Operator::LogicalOr, PseudoType::Refer, PseudoType::Refer },
};

std::vector<UnaryOperator> unary_ops = {
    { Operator::BinaryInvert, TypeKind::IntType, PseudoType::Self },
    { Operator::Idempotent, TypeKind::IntType, PseudoType::Self },
    { Operator::Idempotent, TypeKind::FloatType, PseudoType::Self },
    { Operator::Negate, TypeKind::IntType, PseudoType::Self },
    { Operator::Negate, TypeKind::FloatType, PseudoType::Self },
    { Operator::LogicalInvert, TypeKind::BoolType, PseudoType::Self },
    { Operator::LogicalInvert, TypeKind::OptionalType, PseudoType::Boolean },
    { Operator::LogicalInvert, TypeKind::ResultType, PseudoType::Boolean },
    { Operator::Length, TypeKind::SliceType, PseudoType::Long },
    { Operator::Length, TypeKind::Array, PseudoType::Long },
    { Operator::Length, TypeKind::DynArray, PseudoType::Long },
    { Operator::Length, TypeKind::ZeroTerminatedArray, PseudoType::Long },
    { Operator::Length, TypeKind::EnumType, PseudoType::String },
    { Operator::Unwrap, TypeKind::OptionalType, PseudoType::Refer },
    { Operator::Unwrap, TypeKind::ResultType, PseudoType::Refer },
    { Operator::Unwrap, TypeKind::TaggedUnionType, PseudoType::Refer },
    { Operator::UnwrapError, TypeKind::ResultType, PseudoType::Error },
};

std::map<Operator, Operator> assign_ops = {
    { Operator::AssignAnd, Operator::BinaryAnd },
    { Operator::AssignAnd, Operator::BinaryAnd },
    { Operator::AssignDecrement, Operator::Subtract },
    { Operator::AssignDivide, Operator::Divide },
    { Operator::AssignIncrement, Operator::Add },
    { Operator::AssignModulo, Operator::Modulo },
    { Operator::AssignMultiply, Operator::Multiply },
    { Operator::AssignOr, Operator::BinaryOr },
    { Operator::AssignShiftLeft, Operator::ShiftLeft },
    { Operator::AssignShiftRight, Operator::ShiftRight },
    { Operator::AssignXor, Operator::BinaryXor },
};

Operand::Operand(TypeKind k)
    : type(k)
{
}

Operand::Operand(PseudoType pseudo_type)
    : type(pseudo_type)
{
}

bool Operand::matches(ASTNode const &node, pType const &concrete, pType const &hint) const
{
    auto concrete_value_type = concrete->value_type();
    auto hint_value_type = (hint != nullptr) ? hint->value_type() : nullptr;
    return std::visit(
        overloads {
            [&concrete_value_type](TypeKind k) -> bool {
                return concrete_value_type->is_a(k);
            },
            [&hint_value_type, &concrete_value_type, &node](PseudoType pseudo_type) -> bool {
                switch (pseudo_type) {
                case PseudoType::Boolean:
                    return concrete_value_type == TypeRegistry::boolean;
                case PseudoType::Byte:
                    return concrete_value_type == TypeRegistry::u8;
                case PseudoType::Long:
                    return concrete_value_type == TypeRegistry::i64;
                case PseudoType::String:
                    return concrete_value_type == TypeRegistry::string;
                case PseudoType::Refer: {
                    auto const &refer = std::visit(
                        overloads {
                            [](OptionalType const &optional) -> pType {
                                return optional.type;
                            },
                            [](ResultType const &result) -> pType {
                                return result.success;
                            },
                            [&node](TaggedUnionType const &tagged_union) -> pType {
                                assert(is<TagValue>(node));
                                return TypeRegistry::the().referencing(get<TagValue>(node).payload_type);
                            },
                            [](auto const &) -> pType {
                                UNREACHABLE();
                            } },
                        hint_value_type->description);
                    return concrete_value_type->compatible(refer);
                }
                case PseudoType::Error:
                    assert(is<ResultType>(hint_value_type));
                    return concrete_value_type == get<ResultType>(hint_value_type).error;
                case PseudoType::Lhs:
                case PseudoType::Rhs:
                    return hint_value_type == concrete_value_type;
                default:
                    UNREACHABLE();
                }
            },
        },
        type);
}

bool BinaryOperator::matches(ASTNode const &node, pType const &concrete_lhs, pType const &concrete_rhs) const
{
    return lhs.matches(node, concrete_lhs, concrete_rhs) && rhs.matches(node, concrete_rhs, concrete_lhs);
}

pType BinaryOperator::return_type(ASTNode const &node, pType const &lhs_type, pType const &rhs_type) const
{
    auto lhs_value_type = lhs_type->value_type();
    auto rhs_value_type = (rhs_type != nullptr) ? rhs_type->value_type() : nullptr;
    return std::visit(
        overloads {
            [](TypeKind const &t) -> pType {
                UNREACHABLE();
            },
            [&lhs_value_type, &rhs_value_type, &node](PseudoType pseudo_type) -> pType {
                switch (pseudo_type) {
                case PseudoType::Boolean:
                    return TypeRegistry::boolean;
                case PseudoType::Byte:
                    return TypeRegistry::u8;
                case PseudoType::Long:
                    return TypeRegistry::i64;
                case PseudoType::String:
                    return TypeRegistry::string;
                case PseudoType::Refer:
                    return std::visit(
                        overloads {
                            [](OptionalType const &optional) -> pType {
                                return optional.type;
                            },
                            [](ResultType const &result) -> pType {
                                return result.success;
                            },
                            [&node](TaggedUnionType const &tagged_union) -> pType {
                                assert(is<TagValue>(node));
                                return TypeRegistry::the().referencing(get<TagValue>(node).payload_type);
                            },
                            [](auto const &) -> pType {
                                UNREACHABLE();
                            } },
                        lhs_value_type->description);
                case PseudoType::Error:
                    return get<ResultType>(lhs_value_type).error;
                case PseudoType::Lhs:
                    return lhs_value_type;
                case PseudoType::Rhs:
                    return rhs_value_type;
                default:
                    UNREACHABLE();
                }
            },
        },
        result);
}

}
