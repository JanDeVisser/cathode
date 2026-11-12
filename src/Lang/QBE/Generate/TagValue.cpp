#include <Lang/QBE/Generate/Generate.h>

#include "Lang/QBE/QBE.h"
#include "Lang/SyntaxNode.h"
#include "Lang/Type.h"

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Tag const &impl, QBEContext &)
{
#if 0
    if (impl.operand != nullptr) {
        auto ret = TRY_GENERATE(impl.operand, ctx);
        ret.node = n;
        return ret;
    }
    ILValues values;
    if (impl.payload == nullptr) {
        values.emplace_back(ILValue { });
    } else {
        QBEOperand operand { TRY_GENERATE(impl.payload, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
    }
    auto underlying_type = std::visit(
        overloads {
            [](enumerated_type auto const &enoom) -> ILBaseType {
                return qbe_type_code(enoom.underlying());
            },
            [](auto const &) -> ILBaseType {
                UNREACHABLE();
            } },
        n->bound_type->description);
    values.emplace_back(ILValue::integer(impl.tag_value, underlying_type));
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
#else
    auto underlying_type = std::visit(
        overloads {
            [](enumerated_type auto const &enoom) -> ILBaseType {
                return qbe_type_code(enoom.underlying());
            },
            [](auto const &) -> ILBaseType {
                UNREACHABLE();
            } },
        n->bound_type->description);
    return QBEOperand { n, ILValue::integer(impl.tag_value, underlying_type) };
#endif
}

template<>
GenResult generate_qbe_node(ASTNode const &n, TagPayload const &impl, QBEContext &ctx)
{
    ILValues values;
    if (impl.payload == nullptr) {
        values.emplace_back(ILValue {});
    } else {
        QBEOperand operand { TRY_GENERATE(impl.payload, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
    }
    auto underlying_type = std::visit(
        overloads {
            [](enumerated_type auto const &enoom) -> ILBaseType {
                return qbe_type_code(enoom.underlying());
            },
            [](auto const &) -> ILBaseType {
                UNREACHABLE();
            } },
        n->bound_type->description);
    values.emplace_back(ILValue::integer(impl.tag_value, underlying_type));
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, TagOf const &impl, QBEContext &ctx)
{
    auto const &payload { n->bound_type };
    QBEOperand  operand { TRY_GENERATE(impl.operand, ctx) };
    QBELabel    use_value = { LabelType::Top, n };
    QBELabel    use_alternative = { LabelType::Else, n };
    QBELabel    done = { LabelType::Bottom, n };
    auto        ret = ILValue::local(++ctx.next_var, qbe_type(payload));
    auto        deref = TRY_DEREFERENCE(operand, ctx);
    auto        reference = QBEOperand {
        n,
        TypeRegistry::the().referencing(payload),
        deref.get_value(),
    };
    check_tagged_union(deref, get<TaggedUnionType>(impl.tagged_union), impl.tag_value, use_alternative, use_value, ctx);
    ctx += LabelDef { use_value };
    auto retval = TRY_DEREFERENCE(reference, ctx);
    ctx += JmpDef { done };
    ctx += LabelDef { use_alternative };
    QBEOperand alternative { TRY_GENERATE(impl.alternative, ctx) };
    alternative = TRY_DEREFERENCE(alternative, ctx);
    if (!is<NoReturnType>(impl.alternative->bound_type)) {
        ctx += CopyDef { alternative.get_value(), retval.get_value() };
    }
    ctx += LabelDef { done };
    return retval;
}
}
