#include <Lang/QBE/Generate/Generate.h>

#include "Lang/QBE/QBE.h"
#include "Lang/Type.h"

namespace Lang::QBE {

namespace {
template<typename Range>
    requires(std::derived_from<Range, AbstractSyntaxNode>)
GenResult generate_for_loop(ASTNode const &n, ForStatement const &, Range const &, QBEContext &)
{
    warning("Implement generate_for_loop for `{}`", typeid(Range).name());
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_for_loop(ASTNode const &n, ForStatement const &impl, Tag const &, QBEContext &ctx)
{
    if (!is<TypeType>(impl.range_expr->bound_type)) {
        warning(L"generate_for_loop<TagValue>(): range type `{}` is not a type", impl.range_expr->bound_type->to_string());
        return QBEOperand { n, ILValue::null() };
    }
    auto const &enum_type { get<TypeType>(impl.range_expr->bound_type).type };

    if (!is<EnumType>(enum_type) && !is<TaggedUnionType>(enum_type)) {
        warning(L"generate_for_loop<TagValue>(): range type `{}` is not an enumerated type", impl.range_expr->bound_type->to_string());
        return QBEOperand { n, ILValue::null() };
    }
    auto const &enum_descr { get<EnumType>(enum_type) };

    auto const &underlying { enum_descr.underlying_type };
    auto        init { (n.repo)->make_node<Number>(n->location, underlying, enum_descr.values[0].value) };
    init->bound_type = underlying;
    init->status = ASTStatus::Bound;
    if (auto res = variable_decl(n, impl.range_variable, underlying, init, ctx); !res.has_value()) {
        return std::unexpected(res.error());
    } else {
        auto const &range_var = res.value();
        auto        enum_ptr { ctx.add_enumeration(enum_type) };
        auto        enum_local { ILValue::local(++ctx.next_var, ILBaseType::L) };
        auto        counter { ILValue::local(++ctx.next_var, ILBaseType::L) };
        auto        mapped { ILValue::local(++ctx.next_var, underlying) };
        auto        range_ended { ILValue::local(++ctx.next_var, ILBaseType::W) };
        ctx += LoadDef { enum_ptr, counter },
            ExprDef { enum_ptr, ILValue::integer(size_of(ILBaseType::L), ILBaseType::L), ILOperation::Add, enum_local },
            LabelDef { LabelType::Top, n },
            LoadDef { enum_local, mapped },
            StoreDef { mapped, range_var.get_value() };
        TRY_GENERATE(impl.statement, ctx);
        ctx += ExprDef { counter, ILValue::integer(1, ILBaseType::L), ILOperation::Sub, counter },
            ExprDef { enum_local, ILValue::integer(2 * size_of(ILBaseType::L), ILBaseType::L), ILOperation::Add, enum_local },
            ExprDef { counter, ILValue::integer(0, ILBaseType::L), ILOperation::Greater, range_ended },
            JnzDef { range_ended, QBELabel { LabelType::Top, n }, QBELabel { LabelType::End, n } };
        return QBEOperand { n, ILValue::null() };
    }
}

template<>
GenResult generate_for_loop(ASTNode const &n, ForStatement const &impl, BinaryExpression const &range, QBEContext &ctx)
{
    if (range.op == Operator::MemberAccess && is<TypeType>(range.lhs->bound_type)) {
        return generate_for_loop(n, impl, get<Tag>(range.rhs), ctx);
    }
    if (range.op != Operator::Range) {
        return QBEOperand { n, ILValue::null() };
    }

    auto const &range_start = range.lhs;
    auto const &range_end = TRY_GENERATE(range.rhs, ctx);

    if (auto res = variable_decl(n, impl.range_variable, range_start->bound_type, range_start, ctx); !res.has_value()) {
        return std::unexpected(res.error());
    } else {
        auto const &range_var = res.value();
        auto        range_var_value_pre = ILValue::local(++ctx.next_var, qbe_type(range_start->bound_type));
        auto        range_ended_pre = ILValue::local(++ctx.next_var, ILBaseType::W);
        auto        range_ended = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx += LoadDef { range_var.get_value(), range_var_value_pre },
            ExprDef { range_var_value_pre, range_end.get_value(), ILOperation::Less, range_ended_pre },
            JnzDef { range_ended_pre, QBELabel { LabelType::Top, n }, QBELabel { LabelType::End, n } },
            LabelDef { LabelType::Top, n };
        TRY_GENERATE(impl.statement, ctx);
        auto range_var_value_post = ILValue::local(++ctx.next_var, qbe_type(range_start->bound_type));
        ctx += LoadDef { range_var.get_value(), range_var_value_post };
        auto range_var_value_inc = ILValue::local(++ctx.next_var, qbe_type(range_start->bound_type));
        ctx += ExprDef { range_var_value_post, ILValue::integer(1, range_var.get_value().type), ILOperation::Add, range_var_value_inc },
            StoreDef { range_var_value_inc, range_var.get_value() },
            ExprDef { range_var_value_inc, range_end.get_value(), ILOperation::Less, range_ended },
            JnzDef { range_ended, QBELabel { LabelType::Top, n }, QBELabel { LabelType::End, n } };
        return QBEOperand { n, ILValue::null() };
    }
}

template<>
GenResult generate_for_loop(ASTNode const &n, ForStatement const &impl, SliceIterator const &iter, QBEContext &ctx)
{
    auto const &slice_of = get<SliceType>(impl.range_expr->bound_type).slice_of;

    auto slice_var = TRY_GENERATE(iter.slice, ctx);
    slice_var = TRY_DEREFERENCE(slice_var, ctx);

    auto range_var_type { TypeRegistry::the().referencing(slice_of) };
    auto counter { ILValue::local(++ctx.next_var, ILBaseType::L) };
    auto str_ptr { ILValue::pointer(++ctx.next_var) };
    auto len_ptr { ILValue::pointer(++ctx.next_var) };
    auto len = ILValue::local(++ctx.next_var, ILBaseType::L);
    ctx += LoadDef { slice_var.get_value(), str_ptr },
        ExprDef { slice_var.get_value(), ILValue::integer(sizeof(void *), ILBaseType::L), ILOperation::Add, len_ptr },
        LoadDef { len_ptr, len },
        CopyDef { ILValue::integer(0, ILBaseType::L), counter };

    if (auto res = variable_decl(n, impl.range_variable, range_var_type, nullptr, ctx); !res.has_value()) {
        return std::unexpected(res.error());
    } else {
        auto const &range_var = res.value();
        auto        elem_ptr { ILValue::local(++ctx.next_var, ILBaseType::L) };
        auto        range_empty { ILValue::local(++ctx.next_var, ILBaseType::W) };
        auto        range_ended { ILValue::local(++ctx.next_var, ILBaseType::W) };
        auto        offset { ILValue::local(++ctx.next_var, ILBaseType::L) };
        ctx += ExprDef { ILValue::integer(0, ILBaseType::L), len, ILOperation::Equals, range_empty },
            JnzDef { range_empty, QBELabel { LabelType::Top, n }, QBELabel { LabelType::End, n } },
            LabelDef { LabelType::Top, n },
            ExprDef { counter, ILValue::integer(size_of(slice_of), ILBaseType::L), ILOperation::Mul, offset },
            ExprDef { str_ptr, offset, ILOperation::Add, range_var.get_value() },
            TRY_GENERATE(impl.statement, ctx);
        ctx += ExprDef { counter, ILValue::integer(1, ILBaseType::L), ILOperation::Add, counter },
            ExprDef { counter, len, ILOperation::Less, range_ended },
            JnzDef { range_ended, QBELabel { LabelType::Top, n }, QBELabel { LabelType::End, n } };
        return QBEOperand { n, ILValue::null() };
    }
}

}

template<>
GenResult generate_qbe_node(ASTNode const &n, ForStatement const &impl, QBEContext &ctx)
{
    return std::visit(
        overloads {
            [&n, &impl, &ctx](auto const &range) -> GenResult {
                return generate_for_loop(n, impl, range, ctx);
            },
        },
        n->ns->find_variable(impl.range_variable)->node);
}

}
