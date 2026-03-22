/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <string>
#include <string_view>
#include <variant>

#include <Util/Align.h>
#include <Util/Logging.h>
#include <Util/Options.h>
#include <Util/Pipe.h>
#include <Util/Process.h>
#include <Util/Utf8.h>

#include <App/Config.h>
#include <App/Operator.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/Parser.h>
#include <App/QBE/QBE.h>

namespace Lia::QBE {

static GenResult binexpr(QBEBinExpr const &expr, ILOperation op, ILType type, QBEContext &ctx)
{
    auto    lhs_value = expr.lhs.dereference(ctx).value();
    auto    rhs_value = expr.rhs.dereference(ctx).value();
    int     var = ++ctx.next_var;
    ILValue ret = ILValue::local(var, type);
    ctx.add_operation(
        ExprDef {
            lhs_value.get_value(),
            rhs_value.get_value(),
            op,
            ret,
        });
    return QBEOperand(expr.node, ret);
}

template<class TLeft, class TRight = TLeft>
static GenResult qbe_operator(QBEBinExpr const &expr, TLeft const &lhs, TRight const &rhs, QBEContext &ctx)
{
    fatal(
        L"Invalid operator `{}` `{}` `{}`",
        expr.lhs.node->bound_type->to_string(),
        as_wstring(Operator_name(expr.op)),
        expr.rhs.node->bound_type->to_string());
}

template<>
GenResult qbe_operator(QBEBinExpr const &expr, BoolType const &lhs, BoolType const &rhs, QBEContext &ctx)
{
    ILOperation op;
    switch (expr.op) {
    case Operator::LogicalAnd:
        op = ILOperation::And;
        break;
    case Operator::LogicalOr:
        op = ILOperation::Or;
        break;
    default:
        NYI("QBE mapping for bool operator `{}`", Operator_name(expr.op));
        break;
    }
    return binexpr(expr, op, ILBaseType::W, ctx);
}

static constexpr wchar_t const *division_by_zero { L"Division by zero" };
static constexpr wchar_t const *empty_optional { L"Empty optional value" };
static constexpr wchar_t const *result_error { L"Unwrapping result value containing error" };
static constexpr wchar_t const *result_success { L"Unwrapping error of result value containing success" };
static constexpr wchar_t const *wrong_tagged_value { L"Unwrapping tagged value with wrong value" };

static void check_division_by_zero(QBEOperand const &rhs, QBEContext &ctx)
{
    auto zero { ILValue::local(++ctx.next_var, ILBaseType::W) };
    int  carry_on = ++ctx.next_label;
    int  abort_mission = ++ctx.next_label;
    ctx.add_operation(
        ExprDef {
            ILValue::integer(0, rhs.get_value().type),
            rhs.get_value(),
            ILOperation::Equals,
            zero,
        });
    ctx.add_operation(
        JnzDef {
            zero,
            abort_mission,
            carry_on,
        });
    ctx.add_operation(LabelDef { abort_mission });
    ILValue div_by_zero_id = ctx.add_string(division_by_zero);
    CallDef call_def = {
        L"libliart:lia$abort",
        ILValue::null(),
    };
    call_def.args.push_back(div_by_zero_id);
    call_def.args.push_back(ILValue::integer(wcslen(division_by_zero), ILBaseType::L));
    ctx.add_operation(call_def);
    ctx.add_operation(LabelDef { carry_on });
}

static void check_optional(QBEOperand const &val, OptionalType const &optional, int value_unset, int value_set, QBEContext &ctx)
{
    auto zero { ILValue::local(++ctx.next_var, ILBaseType::W) };
    auto flag_ptr { ILValue::pointer(++ctx.next_var) };
    auto flag_val { ILValue::local(++ctx.next_var, ILBaseType::W) };
    auto flag_val_2 { ILValue::local(++ctx.next_var, ILBaseType::W) };
    ctx.add_operation(
        ExprDef {
            val.get_value(),
            ILValue::integer(optional.type->size_of(), ILBaseType::L),
            ILOperation::Add,
            flag_ptr,
        });
    ctx.add_operation(
        LoadDef {
            flag_ptr,
            flag_val,
        });
    ctx.add_operation(
        ExprDef {
            ILValue::integer(0x00000001, ILBaseType::W),
            flag_val,
            ILOperation::And,
            flag_val_2,
        });
    ctx.add_operation(
        ExprDef {
            ILValue::integer(0, ILBaseType::W),
            flag_val_2,
            ILOperation::Equals,
            zero,
        });
    ctx.add_operation(
        JnzDef {
            zero,
            value_unset,
            value_set,
        });
}

static void check_result(bool success, QBEOperand const &val, ResultType const &result, int value_not_ok, int value_ok, QBEContext &ctx)
{
    auto zero { ILValue::local(++ctx.next_var, ILBaseType::W) };
    auto flag_ptr { ILValue::pointer(++ctx.next_var) };
    auto flag_val { ILValue::local(++ctx.next_var, ILBaseType::W) };
    auto flag_val_2 { ILValue::local(++ctx.next_var, ILBaseType::W) };
    ctx.add_operation(
        ExprDef {
            val.get_value(),
            ILValue::integer(result.flag_offset(), ILBaseType::L),
            ILOperation::Add,
            flag_ptr,
        });
    ctx.add_operation(
        LoadDef {
            flag_ptr,
            flag_val,
        });
    ctx.add_operation(
        ExprDef {
            ILValue::integer(0x01, ILBaseType::W),
            flag_val,
            (success) ? ILOperation::And : ILOperation::Xor,
            flag_val_2,
        });
    ctx.add_operation(
        ExprDef {
            ILValue::integer(0, ILBaseType::W),
            flag_val_2,
            ILOperation::Equals,
            zero,
        });
    ctx.add_operation(
        JnzDef {
            zero,
            value_not_ok,
            value_ok,
        });
}

static void check_tagged_union(int64_t required, QBEOperand const &val, TaggedUnionType const &tagged_union, int value_not_ok, int value_ok, QBEContext &ctx)
{
    auto eql { ILValue::local(++ctx.next_var, ILBaseType::W) };
    auto tag_ptr { ILValue::pointer(++ctx.next_var) };
    auto tag_val { ILValue::local(++ctx.next_var, ILBaseType::W) };
    ctx.add_operation(
        ExprDef {
            val.get_value(),
            ILValue::integer(tagged_union.tag_offset(), ILBaseType::L),
            ILOperation::Add,
            tag_ptr,
        });
    ctx.add_operation(
        LoadDef {
            tag_ptr,
            tag_val,
        });
    ctx.add_operation(
        ExprDef {
            ILValue::integer(required, ILBaseType::W),
            tag_val,
            ILOperation::Equals,
            eql,
        });
    ctx.add_operation(
        JnzDef {
            eql,
            value_ok,
            value_not_ok,
        });
}

static void optional_must(QBEOperand const &val, OptionalType const &optional, QBEContext &ctx)
{
    int carry_on = ++ctx.next_label;
    int abort_mission = ++ctx.next_label;
    check_optional(val, optional, abort_mission, carry_on, ctx);
    ctx.add_operation(LabelDef { abort_mission });
    ILValue empty_optional_id = ctx.add_string(empty_optional);
    CallDef call_def = {
        L"libliart:lia$abort",
        ILValue::null(),
    };
    call_def.args.push_back(empty_optional_id);
    call_def.args.push_back(ILValue::integer(wcslen(empty_optional), ILBaseType::L));
    ctx.add_operation(call_def);
    ctx.add_operation(LabelDef { carry_on });
}

static void result_must(bool success, QBEOperand const &val, ResultType const &result, QBEContext &ctx)
{
    int carry_on = ++ctx.next_label;
    int abort_mission = ++ctx.next_label;
    check_result(success, val, result, abort_mission, carry_on, ctx);
    ctx.add_operation(LabelDef { abort_mission });
    ILValue empty_optional_id = ctx.add_string((success) ? result_error : result_success);
    CallDef call_def = {
        L"libliart:lia$abort",
        ILValue::null(),
    };
    call_def.args.push_back(empty_optional_id);
    call_def.args.push_back(ILValue::integer(wcslen(empty_optional), ILBaseType::L));
    ctx.add_operation(call_def);
    ctx.add_operation(LabelDef { carry_on });
}

static void tagged_union_must(QBEOperand const &val, TaggedUnionType const &tagged_union, QBEContext &ctx)
{
    int carry_on = ++ctx.next_label;
    int abort_mission = ++ctx.next_label;
    assert(is<TagValue>(val.node));
    auto const &tag_value { get<TagValue>(val.node) };
    check_tagged_union(tag_value.tag_value, val, tagged_union, abort_mission, carry_on, ctx);
    ctx.add_operation(LabelDef { abort_mission });
    ILValue empty_optional_id = ctx.add_string(wrong_tagged_value);
    CallDef call_def = {
        L"libliart:lia$abort",
        ILValue::null(),
    };
    call_def.args.push_back(empty_optional_id);
    call_def.args.push_back(ILValue::integer(wcslen(empty_optional), ILBaseType::L));
    ctx.add_operation(call_def);
    ctx.add_operation(LabelDef { carry_on });
}

static GenResult unwrap_optional(ASTNode const &unwrapped, QBEOperand const &optional, QBEContext &ctx)
{
    auto reference = QBEOperand {
        unwrapped,
        TypeRegistry::the().referencing(get<OptionalType>(optional.ptype).type),
        optional.get_value(),
    };
    return TRY_DEREFERENCE(reference, ctx);
}

static GenResult unwrap_result(bool success, ASTNode const &unwrapped, QBEOperand const &result, QBEContext &ctx)
{
    auto result_type = get<ResultType>(result.ptype);
    auto reference = QBEOperand {
        unwrapped,
        TypeRegistry::the().referencing((success) ? result_type.success : result_type.error),
        result.get_value(),
    };
    return TRY_DEREFERENCE(reference, ctx);
}

static GenResult unwrap_tagged_union(ASTNode const &unwrapped, QBEOperand const &result, QBEContext &ctx)
{
    auto tagged_union = get<TaggedUnionType>(result.ptype);
    assert(is<TagValue>(result.node));
    auto const &tag_value { get<TagValue>(result.node) };
    auto        reference = QBEOperand {
        unwrapped,
        TypeRegistry::the().referencing(tag_value.payload_type),
        result.get_value(),
    };
    return TRY_DEREFERENCE(reference, ctx);
}

template<>
GenResult qbe_operator(QBEBinExpr const &expr, IntType const &lhs, IntType const &rhs, QBEContext &ctx)
{
    auto lhs_value = TRY_DEREFERENCE(expr.lhs, ctx);
    auto rhs_value = TRY_DEREFERENCE(expr.rhs, ctx);
    if (expr.op == Operator::Divide || expr.op == Operator::Modulo) {
        check_division_by_zero(rhs_value, ctx);
    }
    ILOperation op;
    ILType      type { lhs_value.get_value().type };
    switch (expr.op) {
    case Operator::Add:
        op = ILOperation::Add;
        break;
    case Operator::BinaryAnd:
        op = ILOperation::And;
        break;
    case Operator::BinaryOr:
        op = ILOperation::Or;
        break;
    case Operator::BinaryXor:
        op = ILOperation::Xor;
        break;
    case Operator::Divide:
        op = (lhs.is_signed) ? ILOperation::Div : ILOperation::UDiv;
        break;
    case Operator::Equals:
        op = ILOperation::Equals;
        type = ILBaseType::W;
        break;
    case Operator::Greater:
        op = ILOperation::Greater;
        type = ILBaseType::W;
        break;
    case Operator::GreaterEqual:
        op = ILOperation::GreaterEqual;
        type = ILBaseType::W;
        break;
    case Operator::Less:
        op = ILOperation::Less;
        type = ILBaseType::W;
        break;
    case Operator::LessEqual:
        op = ILOperation::LessEqual;
        type = ILBaseType::W;
        break;
    case Operator::Modulo:
        op = (lhs.is_signed) ? ILOperation::Mod : ILOperation::UMod;
        type = ILBaseType::W;
        break;
    case Operator::Multiply:
        op = ILOperation::Mul;
        type = ILBaseType::W;
        break;
    case Operator::NotEqual:
        op = ILOperation::NotEqual;
        type = ILBaseType::W;
        break;
    case Operator::ShiftLeft:
        op = ILOperation::Shl;
        break;
    case Operator::ShiftRight:
        op = ILOperation::Shr;
        break;
    case Operator::Subtract:
        op = ILOperation::Sub;
        break;
    default:
        NYI("QBE mapping for int operator `{}`", Operator_name(expr.op));
        break;
    }
    return binexpr(expr, op, type, ctx);
}

template<>
GenResult qbe_operator(QBEBinExpr const &expr, FloatType const &lhs, FloatType const &rhs, QBEContext &ctx)
{
    auto lhs_value = TRY_DEREFERENCE(expr.lhs, ctx);
    auto rhs_value = TRY_DEREFERENCE(expr.rhs, ctx);
    if (expr.op == Operator::Divide || expr.op == Operator::Modulo) {
        check_division_by_zero(rhs_value, ctx);
    }
    ILOperation op;
    switch (expr.op) {
    case Operator::Add:
        op = ILOperation::Add;
        break;
    case Operator::Divide:
        op = ILOperation::Div;
        break;
    case Operator::Equals:
        op = ILOperation::Equals;
        break;
    case Operator::Greater:
        op = ILOperation::Greater;
        break;
    case Operator::GreaterEqual:
        op = ILOperation::GreaterEqual;
        break;
    case Operator::Less:
        op = ILOperation::Less;
        break;
    case Operator::LessEqual:
        op = ILOperation::LessEqual;
        break;
    case Operator::Multiply:
        op = ILOperation::Mul;
        break;
    case Operator::NotEqual:
        op = ILOperation::NotEqual;
        break;
    case Operator::Subtract:
        op = ILOperation::Sub;
        break;
    default:
        NYI("QBE mapping for float operator `{}`", Operator_name(expr.op));
        break;
    }
    return binexpr(expr, op, lhs_value.get_value().type, ctx);
}

GenResult qbe_operator(QBEBinExpr const &expr, StructType const &lhs, auto const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::MemberAccess) {
        auto id = get<Identifier>(expr.rhs.node);
        auto deref = TRY_DEREFERENCE(expr.lhs, ctx).get_value();
        auto ret = ILValue::pointer(++ctx.next_var);
        ctx.add_operation(
            ExprDef {
                deref,
                expr.rhs.get_value(),
                ILOperation::Add,
                ret,
            });
        return QBEOperand { expr.node, ret };
    }
    NYI("QBE mapping for struct operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, OptionalType const &lhs, auto const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::LogicalOr) {
        assert(expr.rhs.ptype == lhs.type);
        int  use_value = ++ctx.next_label;
        int  use_alternative = ++ctx.next_label;
        int  done = ++ctx.next_label;
        auto ret = ILValue::local(++ctx.next_var, qbe_type(lhs.type));
        auto deref = TRY_DEREFERENCE(expr.lhs, ctx);
        check_optional(deref, lhs, use_alternative, use_value, ctx);
        ctx.add_operation(LabelDef { use_alternative });
        auto alternative = TRY_DEREFERENCE(expr.rhs, ctx);
        ctx.add_operation(
            CopyDef {
                alternative.get_value(),
                ret,
            });
        ctx.add_operation(
            JmpDef {
                done,
            });
        ctx.add_operation(LabelDef { use_value });
        auto val = unwrap_optional(expr.node, deref, ctx);
        if (!val) {
            return std::unexpected(val.error());
        }
        ctx.add_operation(
            CopyDef {
                val.value().get_value(),
                ret,
            });
        ctx.add_operation(LabelDef { done });
        return QBEOperand { expr.node, lhs.type, ret };
    }
    NYI("QBE mapping for optional operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, ResultType const &lhs, auto const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::LogicalOr) {
        assert(expr.rhs.ptype->compatible(lhs.success) || expr.rhs.ptype->assignable_to(lhs.success));
        int  use_value = ++ctx.next_label;
        int  use_alternative = ++ctx.next_label;
        int  done = ++ctx.next_label;
        auto ret = ILValue::local(++ctx.next_var, qbe_type(lhs.success));
        auto deref = TRY_DEREFERENCE(expr.lhs, ctx);
        check_result(true, deref, lhs, use_alternative, use_value, ctx);
        ctx.add_operation(LabelDef { use_alternative });
        auto alternative = TRY_DEREFERENCE(expr.rhs, ctx);
        ctx.add_operation(
            CopyDef {
                alternative.get_value(),
                ret,
            });
        ctx.add_operation(
            JmpDef {
                done,
            });
        ctx.add_operation(LabelDef { use_value });
        auto val = unwrap_result(true, expr.node, deref, ctx);
        if (!val) {
            return std::unexpected(val.error());
        }
        ctx.add_operation(
            CopyDef {
                val.value().get_value(),
                ret,
            });
        ctx.add_operation(LabelDef { done });
        return QBEOperand { expr.node, lhs.success, ret };
    }
    NYI("QBE mapping for result operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, TaggedUnionType const &lhs, auto const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::LogicalOr) {
        assert(is<TagValue>(expr.lhs.node));
        auto const &payload { get<TagValue>(expr.lhs.node).payload_type };
        assert(expr.rhs.ptype->compatible(payload) || expr.rhs.ptype->assignable_to(payload));
        int  use_value = ++ctx.next_label;
        int  use_alternative = ++ctx.next_label;
        int  done = ++ctx.next_label;
        auto ret = ILValue::local(++ctx.next_var, qbe_type(payload));
        auto deref = TRY_DEREFERENCE(expr.lhs, ctx);
        check_tagged_union(get<TagValue>(expr.lhs.node).tag_value, deref, lhs, use_alternative, use_value, ctx);
        ctx.add_operation(LabelDef { use_alternative });
        auto alternative = TRY_DEREFERENCE(expr.rhs, ctx);
        ctx.add_operation(
            CopyDef {
                alternative.get_value(),
                ret,
            });
        ctx.add_operation(
            JmpDef {
                done,
            });
        ctx.add_operation(LabelDef { use_value });
        auto val = unwrap_tagged_union(expr.node, deref, ctx);
        if (!val) {
            return std::unexpected(val.error());
        }
        ctx.add_operation(
            CopyDef {
                val.value().get_value(),
                ret,
            });
        ctx.add_operation(LabelDef { done });
        return QBEOperand { expr.node, payload, ret };
    }
    NYI("QBE mapping for result operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, TypeType const &lhs, auto const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::MemberAccess) {
        return QBEOperand { expr.node, expr.rhs.get_value() };
    }
    NYI("QBE mapping for type operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, IntType const &lhs, TypeType const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::Cast) {
        if (expr.lhs.node->bound_type->value_type() == rhs.type) {
            return QBEOperand { expr.node, expr.lhs.get_value() };
        }
        ctx.add_operation(
            ExtDef {
                expr.lhs.get_value(),
                expr.rhs.get_value(),
            });
        return QBEOperand { expr.node, expr.rhs.get_value() };
    }
    NYI("QBE mapping for type operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, EnumType const &lhs, TypeType const &rhs, QBEContext &ctx)
{
    if (expr.op == Operator::Cast) {
        auto deref = TRY_DEREFERENCE(expr.lhs, ctx);
        if (lhs.underlying_type == rhs.type) {
            return deref;
        }

        ctx.add_operation(
            ExtDef {
                deref.get_value(),
                expr.rhs.get_value(),
            });
        return QBEOperand { expr.node, expr.rhs.get_value() };
    }
    NYI("QBE mapping for type operator `{}`", Operator_name(expr.op));
}

GenResult qbe_operator(QBEBinExpr const &expr, QBEContext &ctx)
{
    auto const &lhs_value_type = expr.lhs.node->bound_type->value_type();
    auto const &rhs_value_type = expr.rhs.node->bound_type->value_type();
    QBEBinExpr  expr_materialized {
        expr.node,
        TRY_MATERIALIZE(expr.lhs, ctx),
        expr.op,
        TRY_MATERIALIZE(expr.rhs, ctx),
    };
    return std::visit(
        [&expr_materialized, &ctx](auto const &lhs_descr, auto const &rhs_descr) {
            return qbe_operator(expr_materialized, lhs_descr, rhs_descr, ctx);
        },
        lhs_value_type->description, rhs_value_type->description);
}

template<class T>
static GenResult qbe_operator(QBEUnaryExpr const &expr, T const &operand, QBEContext &ctx)
{
    fatal(L"Invalid operator `{}` `{}` ",
        expr.operand.node->bound_type->to_string(),
        as_wstring(Operator_name(expr.op)));
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, BoolType const &bool_type, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx).get_value();
    auto var = ILValue::local(++ctx.next_var, operand.type);
    switch (expr.op) {
    case Operator::LogicalInvert: {
        ctx.add_operation(
            ExprDef {
                operand,
                ILValue::integer(1, ILBaseType::W),
                ILOperation::Xor,
                var,
            });
        auto ret = ILValue::local(++ctx.next_var, operand.type);
        ctx.add_operation(
            ExprDef {
                var,
                ILValue::integer(1, ILBaseType::W),
                ILOperation::And,
                ret,
            });
        return QBEOperand { expr.node, ret };
    }
    default:
        NYI("QBE mapping for bool operator `{}`", Operator_name(expr.op));
        break;
    }
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, EnumType const &enum_type, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx).get_value();
    if (expr.op == Operator::Length) {
        auto enum_def { ctx.add_enumeration(expr.operand.ptype) };
        auto tag_buffer { ILValue::local(++ctx.next_var, ILBaseType::L) };
        auto ret_val = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            AllocDef {
                16,
                16,
                tag_buffer,
            });
        CallDef call_def = {
            L"libliart:lia$enum_tag",
            ret_val,
        };
        call_def.args.push_back(enum_def);
        call_def.args.push_back(operand);
        call_def.args.push_back(tag_buffer);
        ctx.add_operation(call_def);
        tag_buffer.type = ctx.qbe_type(TypeRegistry::string);
        return QBEOperand { expr.node, tag_buffer };
    }
    NYI("QBE mapping for enum operator `{}`", Operator_name(expr.op));
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, IntType const &int_type, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx);
    auto var = ILValue::local(++ctx.next_var, operand.get_value().type);
    switch (expr.op) {
    case Operator::Negate:
        ctx.add_operation(
            ExprDef {
                operand.get_value(),
                ILValue::null(),
                ILOperation::Neg,
                var,
            });
        return QBEOperand { expr.node, var };
    case Operator::BinaryInvert: {
        ctx.add_operation(
            ExprDef {
                operand.get_value(),
                ILValue::literal(L"~0", operand.get_value().type),
                ILOperation::Xor,
                var,
            });
        auto ret = ILValue::local(++ctx.next_var, operand.get_value().type);
        ctx.add_operation(
            ExprDef {
                var,
                ILValue::literal(L"~0", operand.get_value().type),
                ILOperation::And,
                ret,
            });
        return QBEOperand { expr.node, ret };
    }
    default:
        NYI("QBE mapping for int operator `{}`", Operator_name(expr.op));
        break;
    }
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, FloatType const &float_type, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx);
    auto var = ILValue::local(++ctx.next_var, operand.get_value().type);
    switch (expr.op) {
    case Operator::Negate:
        ctx.add_operation(
            ExprDef {
                operand.get_value(),
                ILValue::null(),
                ILOperation::Neg,
                var,
            });
        return QBEOperand { expr.node, var };
    default:
        NYI("QBE mapping for float operator `{}`", Operator_name(expr.op));
        break;
    }
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, ResultType const &result, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx);
    auto var = ILValue::local(++ctx.next_var, operand.get_value().type);
    switch (expr.op) {
    case Operator::Unwrap: {
        result_must(true, operand, result, ctx);
        return unwrap_result(true, expr.node, operand, ctx);
    } break;
    case Operator::UnwrapError: {
        result_must(false, operand, result, ctx);
        return unwrap_result(false, expr.node, operand, ctx);
    } break;
    case Operator::LogicalInvert: {
        auto flag_ptr = ILValue::pointer(++ctx.next_var);
        ctx.add_operation(
            ExprDef {
                operand.get_value(),
                ILValue::integer(
                    alignat(
                        std::max(result.success->size_of(), result.error->size_of()),
                        std::max(result.success->align_of(), result.error->align_of())),
                    ILBaseType::L),
                ILOperation::Add,
                flag_ptr,
            });
        auto flag_value = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            LoadDef {
                flag_ptr,
                flag_value,
            });
        auto intermediate = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            ExprDef {
                flag_value,
                ILValue::integer(1, ILBaseType::W),
                ILOperation::Xor,
                intermediate,
            });
        auto ret_value = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            ExprDef {
                intermediate,
                ILValue::integer(1, ILBaseType::W),
                ILOperation::And,
                ret_value,
            });
        return QBEOperand { expr.node, ret_value };
    } break;
    default:
        NYI("QBE mapping for optional operator `{}`", Operator_name(expr.op));
        break;
    }
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, OptionalType const &optional, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx);
    auto var = ILValue::local(++ctx.next_var, operand.get_value().type);
    switch (expr.op) {
    case Operator::Unwrap: {
        optional_must(operand, optional, ctx);
        return unwrap_optional(expr.node, operand, ctx);
    } break;
    case Operator::LogicalInvert: {
        auto flag_ptr = ILValue::pointer(++ctx.next_var);
        ctx.add_operation(
            ExprDef {
                operand.get_value(),
                ILValue::integer(optional.type->size_of(), ILBaseType::L),
                ILOperation::Add,
                flag_ptr,
            });
        auto flag_value = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            LoadDef {
                flag_ptr,
                flag_value,
            });
        auto intermediate = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            ExprDef {
                flag_value,
                ILValue::integer(1, ILBaseType::W),
                ILOperation::Xor,
                intermediate,
            });
        auto ret_value = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            ExprDef {
                intermediate,
                ILValue::integer(1, ILBaseType::W),
                ILOperation::And,
                ret_value,
            });
        return QBEOperand { expr.node, ret_value };
    } break;
    default:
        NYI("QBE mapping for optional operator `{}`", Operator_name(expr.op));
        break;
    }
}

template<>
GenResult qbe_operator(QBEUnaryExpr const &expr, TaggedUnionType const &tagged_union, QBEContext &ctx)
{
    auto operand = TRY_DEREFERENCE(expr.operand, ctx);
    auto var = ILValue::local(++ctx.next_var, operand.get_value().type);
    switch (expr.op) {
    case Operator::Unwrap: {
        tagged_union_must(operand, tagged_union, ctx);
        return unwrap_tagged_union(expr.node, operand, ctx);
    } break;
    default:
        NYI("QBE mapping for optional operator `{}`", Operator_name(expr.op));
        break;
    }
}

GenResult qbe_operator(QBEUnaryExpr const &expr, QBEContext &ctx)
{
    auto const &value_type = expr.operand.node->bound_type->value_type();
    if (expr.op == Operator::Idempotent) {
        return QBEOperand { expr.node, expr.operand.get_value() };
    }
    if (expr.op == Operator::AddressOf) {
        return std::visit(
            overloads {
                [&expr](ILValue::Local const &local) -> GenResult {
                    assert(expr.operand.get_value().type == ILBaseType::L);
                    return QBEOperand { expr.node, expr.operand.get_value() };
                },
                [&expr](ILValue::Global const &) -> GenResult {
                    return QBEOperand { expr.node, expr.operand.get_value() };
                },
                [&expr](ILValue::Variable const &variable) -> GenResult {
                    return QBEOperand { expr.node, expr.operand.get_value() };
                },
                [&expr](ILValue::Parameter const &parameter) -> GenResult {
                    return QBEOperand { expr.node, expr.operand.get_value() };
                },
                [](auto const &) -> GenResult {
                    UNREACHABLE();
                    return QBEOperand { nullptr, ILValue::null() };
                } },
            expr.operand.get_value().inner);
        return QBEOperand { expr.node, expr.operand.get_value() };
    }
    QBEUnaryExpr expr_materialized {
        expr.node,
        expr.op,
        TRY_MATERIALIZE(expr.operand, ctx),
    };
    return std::visit(
        [&expr_materialized, &ctx](auto const &descr) {
            return qbe_operator(expr_materialized, descr, ctx);
        },
        value_type->description);
}

}
