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

BinaryExpression::BinaryExpression(ASTNode lhs, Operator const op, ASTNode rhs)
    : lhs(std::move(lhs))
    , op(op)
    , rhs(std::move(rhs))
{
}

BindResult BinaryExpression::bind(ASTNode const &n) const
{
    assert(n != nullptr);
    Parser &parser = *(n.repo);
    auto    lhs_type = try_bind(lhs);

    if (op == Operator::MemberAccess) {
        if (is<TypeType>(lhs_type)) {
            auto type_type = get<TypeType>(lhs_type).type;
            if (!is<Identifier>(rhs)) {
                return parser.bind_error(rhs->location,
                    L"The right-hand side of a member access must be an identifier");
            }
            auto const &label = get<Identifier>(rhs).identifier;
            if (auto res = std::visit(
                    overloads {
                        [&label, this, &n, &type_type](TaggedUnionType const &t) -> std::expected<ASTNode, LiaError> {
                            if (auto v = t.value_for(label); v) {
                                auto ret = make_node<TagValue>(n, static_cast<int64_t>(*v), label, t.payload_for(label), nullptr);
                                ret->bound_type = type_type;
                                ret->status = ASTStatus::Bound;
                                return ret;
                            }
                            return std::unexpected(
                                LiaError {
                                    rhs->location,
                                    std::format(L"Unknown tagged union value `{}`", label),
                                });
                        },
                        [&label, this, &n, &type_type](EnumType const &t) -> std::expected<ASTNode, LiaError> {
                            if (auto v = t.value_for(label); v) {
                                auto ret = make_node<TagValue>(n, static_cast<int64_t>(*v), label, nullptr, nullptr);
                                ret->bound_type = type_type;
                                ret->status = ASTStatus::Bound;
                                return ret;
                            }
                            return std::unexpected(
                                LiaError {
                                    rhs->location,
                                    std::format(L"Unknown enum value `{}`", label),
                                });
                        },
                        [this](auto const &) -> std::expected<ASTNode, LiaError> {
                            return std::unexpected(
                                LiaError {
                                    lhs->location,
                                    L"A type in the left-hand side of a member access must be an `enum` type",
                                });
                        } },
                    type_type->description);
                !res) {
                return parser.bind_error(res.error());
            } else {
                return type_type;
            };
        }
        if ((lhs_type->kind() != TypeKind::ModuleType) && (lhs_type->kind() != TypeKind::ReferenceType && !is<Identifier>(lhs))) {
            return parser.bind_error(
                lhs->location,
                L"Left hand side of member access operator must be value reference or module");
        }
        auto lhs_value_type = lhs_type->value_type();
        return std::visit(
            overloads {
                [this, &n](ModuleType const &) -> BindResult {
                    auto       proxy { get<ModuleProxy>(lhs) };
                    auto const label { get<Identifier>(rhs).identifier };
                    auto       t { proxy.module->ns->type_of(label) };
                    if (t == nullptr) {
                        return BindError { ASTStatus::Undetermined };
                    }
                    rhs->bound_type = t;
                    rhs->status = ASTStatus::Bound;
                    auto var { proxy.module->ns->find_variable(label) };
                    if (var != nullptr && is_constant(var)) {
                        n->superceded_by = get<VariableDeclaration>(var).initializer;
                    }
                    return t;
                },
                [&parser, this](StructType const &strukt) -> BindResult {
                    auto find_member = [&strukt](std::wstring_view const name) -> std::expected<StructType::Field, std::wstring> {
                        for (auto const &field : strukt.fields) {
                            if (field.name == name) {
                                return field;
                            }
                        }
                        return std::unexpected(std::format(L"Unknown struct field `{}`", name));
                    };
                    if (auto struct_field = find_member(get<Identifier>(rhs).identifier); !struct_field) {
                        return parser.bind_error(rhs->location, struct_field.error());
                    } else {
                        rhs->bound_type = TypeRegistry::string;
                        rhs->status = ASTStatus::Bound;
                        return TypeRegistry::the().referencing(struct_field->type);
                    }
                },
                [&parser, this, &lhs_value_type, &n](TaggedUnionType const &tagged_union) -> BindResult {
                    if (!is<Identifier>(rhs)) {
                        return parser.bind_error(rhs->location,
                            L"The right-hand side of a member access must be an identifier");
                    }
                    auto const &label = get<Identifier>(rhs).identifier;
                    if (auto v = tagged_union.value_for(label); v) {
                        make_node<TagValue>(n, lhs, static_cast<int64_t>(*v), label, tagged_union.payload_for(label), nullptr);
                        return lhs_value_type;
                    }
                    return parser.bind_error(
                        rhs->location,
                        L"Unknown tagged union value `{}`", label);
                },
                [&parser, &lhs_value_type, this](auto const &) -> BindResult {
                    return parser.bind_error(
                        rhs->location,
                        L"Left hand side of member access operator has type `{}` which is not a struct", lhs_value_type->name);
                } },
            lhs_value_type->description);
    }

    auto lhs_value_type = lhs_type->value_type();
    auto rhs_type = try_bind(rhs);
    auto rhs_value_type = rhs_type->value_type();

    if (op == Operator::Assign) {
        if (is<TagValue>(lhs)) {
            if (!is<TaggedUnionType>(lhs_value_type)) {
                return parser.bind_error(lhs->location, L"Only tagged union values can be assigned a payload");
            }
            auto const &tag_value { get<TagValue>(lhs) };
            if (tag_value.payload != nullptr) {
                return parser.bind_error(rhs->location, L"Can only attach one payload to a tagged value");
            }
            if (tag_value.payload_type == nullptr || tag_value.payload_type == TypeRegistry::void_) {
                return parser.bind_error(
                    n->location,
                    L"Value `{}` of tagged union type `{}` does not take a payload",
                    tag_value.label, lhs_value_type->name);
            }

            if (!tag_value.payload_type->compatible(rhs_value_type)
                && !rhs_value_type->assignable_to(tag_value.payload_type)) {
                return parser.bind_error(
                    n->location,
                    L"Tagged union value `{}` requires payload of type `{}` instead of `{}`",
                    tag_value.label, tag_value.payload_type->name, rhs_value_type->name);
            }

            make_node<TagValue>(
                n,
                tag_value.tag_value,
                tag_value.label,
                tag_value.payload_type,
                rhs);
            return lhs_value_type;
        }
        if (!lhs_value_type->compatible(rhs_value_type) && !rhs_value_type->assignable_to(lhs_value_type)) {
            return parser.bind_error(
                n->location,
                L"Cannot assign a value of type `{}` to a variable of type `{}`",
                rhs_type->name,
                lhs_type->name);
        }
        return lhs_type;
    }

    if (op == Operator::Range) {
        if (lhs_value_type == rhs_value_type
            && (is<IntType>(lhs_value_type) || is<EnumType>(lhs_value_type))) {
            return TypeRegistry::the().range_of(lhs_value_type);
        }
    }

    if (op == Operator::Call && is<FunctionType>(lhs_value_type) && is<TypeList>(rhs_value_type)) {
        fatal("The `Call` binary operator should be elided during the normalization phase");
    }

    if (op == Operator::Cast) {
        if (rhs_value_type->kind() != TypeKind::TypeType) {
            return parser.bind_error(
                rhs->location,
                L"`Cast` operator requires a type as right hand side");
        }
        auto target_type = get<TypeType>(rhs_value_type).type;
        return std::visit(
            overloads {
                [&target_type, &n, &parser](IntType const &lhs_int_type, IntType const &rhs_int_type) -> BindResult {
                    if (lhs_int_type.width_bits > rhs_int_type.width_bits) {
                        return parser.bind_error(
                            n->location,
                            L"Invalid argument type. Cannot narrow integers");
                    }
                    return { target_type };
                },
                [&target_type, &n, &parser](EnumType const &lhs_enum_type, IntType const &rhs_int_type) -> BindResult {
                    auto lhs_int { lhs_enum_type.underlying_type };
                    assert(std::holds_alternative<IntType>(lhs_int->description));
                    auto lhs_int_type { std::get<IntType>(lhs_int->description) };
                    if (lhs_int_type.width_bits > rhs_int_type.width_bits) {
                        return parser.bind_error(
                            n->location,
                            L"Invalid argument type. Cannot narrow integers");
                    }
                    return { target_type };
                },
                [&target_type, &n, &parser](IntType const &lhs_int_type, EnumType const &rhs_enum_type) -> BindResult {
                    auto rhs_int { rhs_enum_type.underlying_type };
                    assert(std::holds_alternative<IntType>(rhs_int->description));
                    auto rhs_int_type { std::get<IntType>(rhs_int->description) };
                    if (lhs_int_type.width_bits > rhs_int_type.width_bits) {
                        return parser.bind_error(
                            n->location,
                            L"Invalid argument type. Cannot narrow integers");
                    }
                    return { target_type };
                },
                [&target_type, &n, &parser](SliceType const &lhs_slice_type, ZeroTerminatedArray const &rhs_zero_terminated_type) -> BindResult {
                    if (lhs_slice_type.slice_of != TypeRegistry::u32 || rhs_zero_terminated_type.array_of != TypeRegistry::u8) {
                        return parser.bind_error(
                            n->location,
                            L"Invalid argument type. Cannot cast slices to zero-terminated arrays except for strings");
                    }
                    return { target_type };
                },
                [&parser, &n](auto const &, auto const &) -> BindResult {
                    return parser.bind_error(
                        n->location,
                        L"Invalid argument type. Can only cast integers");
                } },
            lhs_value_type->description, target_type->description);
    }

    auto check_operators = [this](Operator op, pType op_lhs_type, pType op_rhs_type) -> pType {
        for (auto const &o : binary_ops) {
            if (op == o.op && o.matches(lhs, op_lhs_type, op_rhs_type)) {
                return o.return_type(lhs, op_lhs_type, op_rhs_type);
            }
        }
        return nullptr;
    };

    if (auto result = check_operators(op, lhs_value_type, rhs_value_type); result != nullptr) {
        return result;
    }
    if (auto const rhs_coerced_to_lhs = coerce(rhs, lhs_value_type); rhs_coerced_to_lhs != nullptr) {
        if (auto result = check_operators(op, lhs_value_type, rhs_coerced_to_lhs->bound_type); result != nullptr) {
            make_node<BinaryExpression>(n, lhs, op, rhs_coerced_to_lhs);
            return result;
        }
    }
    if (auto const lhs_coerced_to_rhs = coerce(lhs, rhs_value_type); lhs_coerced_to_rhs != nullptr) {
        if (auto result = check_operators(op, lhs_coerced_to_rhs->bound_type, rhs_value_type); result != nullptr) {
            make_node<BinaryExpression>(n, lhs_coerced_to_rhs, op, rhs);
            return result;
        }
    }

    return parser.bind_error(
        n->location,
        L"Operator `{}` cannot be applied to left hand type `{}` and right hand type `{}`",
        as_wstring(Operator_name(op)),
        lhs_value_type->name,
        rhs_value_type->name);
}

ExpressionList::ExpressionList(ASTNodes expressions)
    : expressions(std::move(expressions))
{
}

BindResult ExpressionList::bind(ASTNode const &) const
{
    return TypeRegistry::the().typelist_of(try_bind_nodes(expressions));
}

UnaryExpression::UnaryExpression(Operator const op, ASTNode operand)
    : op(op)
    , operand(std::move(operand))
{
}

BindResult UnaryExpression::bind(ASTNode const &n) const
{
    assert(n != nullptr);
    auto &parser { *(n.repo) };
    auto  operand_type = try_bind(operand);
    if (op == Operator::Sizeof) {
        return TypeRegistry::i64;
    }
    if (op == Operator::AddressOf) {
        if (is<ReferenceType>(operand_type)) {
            return parser.bind_error(n->location, L"Cannot take address of reference");
        }
        if (is_constant(n)) {
            return parser.bind_error(n->location, L"Cannot take address of constant");
        }
        return TypeRegistry::the().referencing(operand_type);
    }
    for (auto const &[oper, the_operand, result] : unary_ops) {

        if (op == oper && the_operand.matches(operand, operand_type)) {
            return std::visit(
                overloads {
                    [](TypeKind const &) -> pType {
                        UNREACHABLE();
                    },
                    [&operand_type, this](PseudoType const &pseudo_type) -> pType {
                        switch (pseudo_type) {
                        case PseudoType::Self:
                            return operand_type;
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
                                    [](OptionalType const &opt) -> pType {
                                        return TypeRegistry::the().referencing(opt.type);
                                    },
                                    [](ResultType const &result) -> pType {
                                        return TypeRegistry::the().referencing(result.success);
                                    },
                                    [this](TaggedUnionType const &) -> pType {
                                        assert(operand->type() == SyntaxNodeType::TagValue);
                                        return TypeRegistry::the().referencing(get<TagValue>(operand).payload_type);
                                    },
                                    [](auto const &) -> pType {
                                        UNREACHABLE();
                                        return TypeRegistry::void_;
                                    } },
                                operand_type->description);
                        case PseudoType::Error:
                            return std::visit(
                                overloads {
                                    [](ResultType const &result) -> pType {
                                        return TypeRegistry::the().referencing(result.error);
                                    },
                                    [](auto const &) -> pType {
                                        UNREACHABLE();
                                        return TypeRegistry::void_;
                                    } },
                                operand_type->description);
                        default:
                            UNREACHABLE();
                        }
                    } },
                result);
        }
    }
    return parser.bind_error(
        n->location,
        L"Unary operator `{}` cannot be applied to type `{}`",
        as_wstring(Operator_name(op)),
        operand_type->name);
}

}
