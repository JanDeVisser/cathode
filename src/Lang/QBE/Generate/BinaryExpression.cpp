#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

GenBinExpr make_binexpr(ASTNode const &n, QBEOperand lhs, Operator op, QBEOperand rhs, QBEContext &ctx)
{
    pType const &lhs_type = lhs.ptype;
    pType const &rhs_type = rhs.ptype;
    if (is<ReferenceType>(lhs_type) || is<ReferenceType>(rhs_type)) {
        return std::visit(
            overloads {
                [&n, &lhs, &op, &rhs, &ctx](ReferenceType const &, auto const &) -> GenBinExpr {
                    lhs = TRY_ENSURE_GENERATED(lhs, ctx);
                    lhs = TRY_DEREFERENCE(lhs, ctx);
                    return make_binexpr(n, lhs, op, rhs, ctx);
                },
                [&n, &lhs, &op, &rhs, &ctx](auto const &, ReferenceType const &) -> GenBinExpr {
                    rhs = TRY_ENSURE_GENERATED(rhs, ctx);
                    rhs = TRY_DEREFERENCE(rhs, ctx);
                    return make_binexpr(n, lhs, op, rhs, ctx);
                },
                [&n, &lhs, &op, &rhs, &ctx](ReferenceType const &, ReferenceType const &) -> GenBinExpr {
                    lhs = TRY_ENSURE_GENERATED(lhs, ctx);
                    lhs = TRY_DEREFERENCE(lhs, ctx);
                    rhs = TRY_ENSURE_GENERATED(rhs, ctx);
                    rhs = TRY_DEREFERENCE(rhs, ctx);
                    return make_binexpr(n, lhs, op, rhs, ctx);
                },
                [](auto const &, auto const &) -> GenBinExpr {
                    UNREACHABLE();
                }

            },
            lhs_type->description, rhs_type->description);
    }
    return std::visit(
        overloads {
            [&n, &lhs, &op, &rhs, &ctx](StructType const &strukt, auto const &) -> GenBinExpr {
                if (op == Operator::MemberAccess) {
                    lhs = TRY_ENSURE_GENERATED(lhs, ctx);
                    auto const &member = get<Identifier>(rhs.node).identifier;
                    rhs.set_value(ILValue::integer(strukt.offset_of(member), ILBaseType::L));
                }
                return QBEBinExpr { n, lhs, op, rhs };
            },
            [&n, &lhs, &op, &rhs](TypeType const &meta_type, auto const &) -> GenBinExpr {
                auto type = meta_type.type;
                if (op == Operator::MemberAccess) {
                    auto const &label = get<Identifier>(rhs.node).identifier;
                    std::visit(
                        overloads {
                            [&label, &lhs, &rhs](enumerated_type auto const &enoom) {
                                lhs.set_value(ILValue::null());
                                auto value = enoom.value_for(label);
                                assert(value);
                                rhs.set_value(ILValue::integer(*value, qbe_type(enoom.underlying())));
                            },
                            [](auto const &) {
                                UNREACHABLE();
                            } },
                        type->description);
                }
                return QBEBinExpr { n, lhs, op, rhs };
            },
            [&n, &lhs, &op, &rhs, &ctx](IntType const &, TypeType const &cast_to) -> GenBinExpr {
                rhs.set_value(ILValue::local(++ctx.next_var, qbe_type(cast_to.type)));
                return QBEBinExpr { n, TRY_ENSURE_GENERATED(lhs, ctx), op, rhs };
            },
            [&n, &lhs, &op, &rhs, &ctx](EnumType const &, TypeType const &cast_to) -> GenBinExpr {
                rhs.set_value(ILValue::local(++ctx.next_var, qbe_type(cast_to.type)));
                return QBEBinExpr { n, TRY_ENSURE_GENERATED(lhs, ctx), op, rhs };
            },
            [&n, &lhs, &op, &rhs, &ctx](auto const &, auto const &) -> GenBinExpr {
                return QBEBinExpr { n, TRY_ENSURE_GENERATED(lhs, ctx), op, TRY_ENSURE_GENERATED(rhs, ctx) };
            } },
        lhs_type->description, rhs_type->description);
}

template<>
GenResult generate_qbe_node(ASTNode const &n, BinaryExpression const &impl, QBEContext &ctx)
{
    assert(impl.lhs->bound_type != nullptr);
    assert(impl.rhs->bound_type != nullptr);
    trace(QBE, L"Generating BinExpr {} {} {}", impl.lhs->bound_type->name, as_wstring(Operator_name(impl.op)), impl.rhs->bound_type->name);

    if (impl.op == Operator::Assign) {
        return assign(TRY_GENERATE(impl.lhs, ctx), impl.rhs, ctx);
    }
    QBEOperand lhs_operand { impl.lhs };
    QBEOperand rhs_operand { impl.rhs };
    auto       res = make_binexpr(n, lhs_operand, impl.op, rhs_operand, ctx);
    if (!res) {
        return std::unexpected(res.error());
    }
    trace(QBE, L"binexpr {} {} {}", res.value().lhs.get_value(), as_wstring(Operator_name(res.value().op)), res.value().rhs.get_value());
    return qbe_operator(res.value(), ctx);
}

}
