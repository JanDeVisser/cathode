/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <expected>
#include <ranges>
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

using QBEContexts = std::vector<QBEContext>;

GenResult generate_qbe_node(ASTNode const &, QBEContext &);
GenResult generate_qbe_nodes(ASTNodes const &, QBEContext &);

GenResult QBEOperand::get_value(QBEContext &ctx)
{
    if (!value) {
        if (auto res = generate_qbe_node(node, ctx); !res) {
            return res;
        } else {
            *this = res.value();
        }
    }
    return *this;
}

static intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, pType const &type, QBEContext &ctx);

template<class T>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, T const &, QBEContext &ctx)
{
    trace(L"raw_assign({})", demangle<T>());
    ctx.add_operation(
        StoreDef {
            rhs,
            lhs,
        });
    return size_of(rhs.type);
}

template<>
intptr_t raw_assign(ILValue const &, ILValue const &, std::monostate const &, QBEContext &)
{
    return 0;
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, StructType const &strukt, QBEContext &ctx)
{
    return std::visit(
        overloads {
            [&ctx, &lhs, &strukt](ILValues const &values) {
                auto     lhs_ptr { lhs };
                ILValue  prev_lhs;
                auto     prev_size { 0 };
                intptr_t total_size { 0 };
                for (auto const &[ix, value] : std::ranges::views::enumerate(values)) {
                    auto const &fld { strukt.fields[ix] };
                    if (prev_size > 0) {
                        lhs_ptr = ILValue::pointer(++ctx.next_var);
                        ctx.add_operation(
                            ExprDef {
                                prev_lhs,
                                ILValue::integer(alignat(prev_size, fld.type->align_of()), ILBaseType::L),
                                ILOperation::Add,
                                lhs_ptr,
                            });
                    }
                    prev_size = raw_assign(lhs_ptr, value, fld.type, ctx);
                    total_size += prev_size;
                    prev_lhs = lhs_ptr;
                }
                return total_size;
            },
            [&ctx, &lhs, &rhs, &strukt](auto const & /* pointer */) {
                ctx.add_operation(
                    BlitDef {
                        rhs,
                        lhs,
                        static_cast<intptr_t>(strukt.size_of()),
                    });
                return strukt.size_of();
            } },
        rhs.inner);
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, TypeList const &type_list, QBEContext &ctx)
{
    return std::visit(
        overloads {
            [&ctx, &lhs, &type_list](ILValues const &values) {
                auto     lhs_ptr { lhs };
                ILValue  prev_lhs;
                auto     prev_size { 0 };
                intptr_t total_size { 0 };
                for (auto const &[value, t] : std::ranges::views::zip(values, type_list.types)) {
                    if (prev_size > 0) {
                        lhs_ptr = ILValue::pointer(++ctx.next_var);
                        ctx.add_operation(
                            ExprDef {
                                prev_lhs,
                                ILValue::integer(alignat(prev_size, t->align_of()), ILBaseType::L),
                                ILOperation::Add,
                                lhs_ptr,
                            });
                    }
                    prev_size = raw_assign(lhs_ptr, value, t, ctx);
                    total_size += prev_size;
                    prev_lhs = lhs_ptr;
                }
                return total_size;
            },
            [&ctx, &lhs, &rhs, &type_list](auto const & /* pointer */) {
                ctx.add_operation(
                    BlitDef {
                        rhs,
                        lhs,
                        static_cast<intptr_t>(type_list.size_of()),
                    });
                return type_list.size_of();
            } },
        rhs.inner);
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, OptionalType const &optional, QBEContext &ctx)
{
    ctx.add_operation(
        BlitDef {
            rhs,
            lhs,
            static_cast<intptr_t>(optional.size_of()),
        });
    return optional.size_of();
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, ResultType const &result, QBEContext &ctx)
{
    ctx.add_operation(
        BlitDef {
            rhs,
            lhs,
            static_cast<intptr_t>(result.size_of()),
        });
    return result.size_of();
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, SliceType const &slice, QBEContext &ctx)
{
    ctx.add_operation(
        BlitDef {
            rhs,
            lhs,
            static_cast<intptr_t>(slice.size_of()),
        });
    return slice.size_of();
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, EnumType const &enoom, QBEContext &ctx)
{
    return std::visit(
        overloads {
            [&ctx, &lhs](ILValues const &values) {
                assert(values.size() == 2);
                return raw_assign(lhs, values[1], TypeRegistry::i64, ctx);
            },
            [&ctx, &lhs, &rhs, &enoom](auto const & /*pointer*/) {
                ctx.add_operation(
                    StoreDef {
                        rhs,
                        lhs,
                    });
                return enoom.size_of();
            } },
        rhs.inner);
}

template<>
intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, TaggedUnionType const &tagged_union, QBEContext &ctx)
{
    return std::visit(
        overloads {
            [&ctx, &lhs, &tagged_union](ILValues const &values) {
                assert(values.size() == 2);
                auto tag_value = values[1];
                auto ret = raw_assign(lhs, values[0], tagged_union.payload_for(std::get<int64_t>(tag_value.inner)), ctx);
                auto lhs_ptr = ILValue::pointer(++ctx.next_var);
                ctx.add_operation(
                    ExprDef {
                        lhs,
                        ILValue::integer(tagged_union.tag_offset(), ILBaseType::L),
                        ILOperation::Add,
                        lhs_ptr,
                    });
                ret += raw_assign(lhs_ptr, values[1], TypeRegistry::i64, ctx);
                return ret;
            },
            [&ctx, &lhs, &rhs, &tagged_union](auto const & /*pointer*/) {
                ctx.add_operation(
                    BlitDef {
                        rhs,
                        lhs,
                        static_cast<intptr_t>(tagged_union.size_of()),
                    });
                return tagged_union.size_of();
            } },
        rhs.inner);
}

template<>
intptr_t raw_assign(ILValue const &, ILValue const &, VoidType const &, QBEContext &)
{
    return 0;
}

intptr_t raw_assign(ILValue const &lhs, ILValue const &rhs, pType const &type, QBEContext &ctx)
{
    return std::visit(
        [&ctx, &lhs, &rhs](auto const &descr) {
            return raw_assign(lhs, rhs, descr, ctx);
        },
        type->description);
}

GenResult QBEOperand::materialize(QBEContext &ctx) const
{
    assert(value.has_value());
    if (std::holds_alternative<ILValue::ILValues>(value->inner)) {
        auto ret_op { *this };
        auto temp { ctx.add_temporary(ptype) };
        ret_op.set_value(ILValue::temporary(temp.index, ptype));
        raw_assign(ret_op.get_value(), get_value(), ptype, ctx);
        return ret_op;
    }
    return *this;
}

GenResult QBEOperand::dereference(QBEContext &ctx) const
{
    auto ret_op { *this };
    if (is<ReferenceType>(ptype)) {
        assert(value.has_value());
        ret_op.ptype = get<ReferenceType>(ptype).referencing;
        if (qbe_first_class_type(ret_op.ptype)) {
            ret_op.set_value(ILValue::local(++ctx.next_var, qbe_load_code(ret_op.ptype)));
            ctx.add_operation(
                LoadDef {
                    .pointer = get_value(),
                    .target = ret_op.get_value(),
                });
        }
        return ret_op;
    }
    std::visit(
        overloads {
            [&ctx, &ret_op, this](ILValue::Variable const &) {
                if (qbe_first_class_type(ret_op.ptype)) {
                    ret_op.set_value(ILValue::local(++ctx.next_var, ctx.qbe_type(ret_op.ptype)));
                    ctx.add_operation(
                        LoadDef {
                            .pointer = get_value(),
                            .target = ret_op.get_value(),
                        });
                }
            },
            [&ctx, &ret_op, this](ILValue::Parameter const &) {
                if (qbe_first_class_type(ret_op.ptype)) {
                    ret_op.set_value(ILValue::local(++ctx.next_var, ctx.qbe_type(ret_op.ptype)));
                    ctx.add_operation(
                        LoadDef {
                            .pointer = get_value(),
                            .target = ret_op.get_value(),
                        });
                }
            },
            [](auto const &) {
            } },
        get_value().inner);
    return ret_op;
}

GenResult assign(QBEOperand const &lhs, ASTNode const &rhs, QBEContext &ctx)
{
    auto rhs_value = TRY_GENERATE(rhs, ctx);
    rhs_value = TRY_DEREFERENCE(rhs_value, ctx);
    std::visit(
        overloads {
            [&ctx, &lhs, &rhs_value](OptionalType const &lhs_opt, OptionalType const &rhs_opt) {
                assert(lhs_opt.type == rhs_opt.type);
                raw_assign(lhs.get_value(), rhs_value.get_value(), lhs.ptype, ctx);
            },
            [&ctx, &lhs](OptionalType const &optional, VoidType const &) {
                auto flag_ptr = ILValue::pointer(++ctx.next_var);
                ctx.add_operation(
                    ExprDef {
                        lhs.get_value(),
                        ILValue::integer(optional.type->size_of(), ILBaseType::L),
                        ILOperation::Add,
                        flag_ptr,
                    });
                ctx.add_operation(
                    StoreDef {
                        ILValue::integer(0, ILBaseType::B),
                        flag_ptr,
                    });
            },
            [&ctx, &lhs, &rhs_value](OptionalType const &optional, auto const &) {
                raw_assign(lhs.get_value(), rhs_value.get_value(), optional.type, ctx);
                auto flag_ptr = ILValue::pointer(++ctx.next_var);
                ctx.add_operation(
                    ExprDef {
                        lhs.get_value(),
                        ILValue::integer(optional.type->size_of(), ILBaseType::L),
                        ILOperation::Add,
                        flag_ptr,
                    });
                ctx.add_operation(
                    StoreDef {
                        ILValue::integer(1, ILBaseType::B),
                        flag_ptr,
                    });
            },
            [&ctx, &lhs, &rhs_value](ResultType const &, ResultType const &) {
                // Make more sophisticated so that forwarding of errors becomes
                // more convenient.
                raw_assign(lhs.get_value(), rhs_value.get_value(), lhs.ptype, ctx);
            },
            [&ctx, &lhs, &rhs, &rhs_value](ResultType const &result, auto const &) {
                assert(rhs->bound_type == result.success || rhs->bound_type == result.error);
                auto flag_value = (rhs->bound_type == result.success) ? 1 : 0;
                if (rhs->bound_type == result.success) {
                    raw_assign(lhs.get_value(), rhs_value.get_value(), result.success, ctx);
                } else {
                    raw_assign(lhs.get_value(), rhs_value.get_value(), result.error, ctx);
                }
                auto flag_ptr = ILValue::pointer(++ctx.next_var);
                ctx.add_operation(
                    ExprDef {
                        lhs.get_value(),
                        ILValue::integer(result.flag_offset(), ILBaseType::L),
                        ILOperation::Add,
                        flag_ptr,
                    });
                ctx.add_operation(
                    StoreDef {
                        ILValue::integer(flag_value, ILBaseType::B),
                        flag_ptr,
                    });
            },
            [&ctx, &lhs, &rhs_value](auto const &lhs_descr, auto const &rhs_descr) {
                trace(L"generic assign({}, {})", demangle<decltype(lhs_descr)>(), demangle<decltype(rhs_descr)>());
                raw_assign(lhs.get_value(), rhs_value.get_value(), lhs.ptype, ctx);
            } },
        lhs.ptype->description, rhs->bound_type->description);
    return lhs;
}

GenResult cast(QBEOperand value, pType const &target_type, QBEContext &ctx)
{
    if (value.ptype == target_type) {
        return value;
    }
    QBEOperand ret_op { value };
    ret_op.ptype = target_type;
    return std::visit(
        overloads {
            [&value, &ctx, &target_type](ReferenceType const &, auto const &) -> GenResult {
                return cast(TRY_DEREFERENCE(value, ctx), target_type, ctx);
            },
            [&ctx, &value, &ret_op](OptionalType const &optional, BoolType const &) -> GenResult {
                auto flag_ptr = ILValue::pointer(++ctx.next_var);
                auto ret_value = ILValue::local(++ctx.next_var, ILBaseType::SB);
                ctx.add_operation(
                    ExprDef {
                        value.get_value(),
                        ILValue::integer(optional.type->size_of(), ILBaseType::L),
                        ILOperation::Add,
                        flag_ptr,
                    });
                ctx.add_operation(
                    LoadDef {
                        flag_ptr,
                        ret_value,
                    });
                ret_op.set_value(ret_value);
                return ret_op;
            },
            [&ctx, &value, &ret_op](ResultType const &result, BoolType const &) -> GenResult {
                auto flag_ptr = ILValue::pointer(++ctx.next_var);
                auto ret_value = ILValue::local(++ctx.next_var, ILBaseType::SB);
                ctx.add_operation(
                    ExprDef {
                        value.get_value(),
                        ILValue::integer(result.flag_offset(), ILBaseType::L),
                        ILOperation::Add,
                        flag_ptr,
                    });
                ctx.add_operation(
                    LoadDef {
                        flag_ptr,
                        ret_value,
                    });
                ret_op.set_value(ret_value);
                return ret_op;
            },
            [&ctx, &value, &ret_op](IntType const &, BoolType const &) -> GenResult {
                auto ret_value = ILValue::local(++ctx.next_var, ILBaseType::SB);
                ctx.add_operation(
                    ExprDef {
                        value.get_value(),
                        ILValue::integer(0, qbe_type_code(value.ptype)),
                        ILOperation::Equals,
                        ret_value,
                    });
                ret_op.set_value(ret_value);
                return ret_op;
            },
            [&ctx, &value, &ret_op](IntType const &, IntType const &) -> GenResult {
                auto ret_value = ILValue::local(++ctx.next_var, ILBaseType::SB);
                ctx.add_operation(
                    ExprDef {
                        value.get_value(),
                        ILValue::integer(0, qbe_type_code(value.ptype)),
                        ILOperation::Equals,
                        ret_value,
                    });
                ret_op.set_value(ret_value);
                return ret_op;
            },
            [](auto const &, auto const &) -> GenResult {
                return std::unexpected(L"Invalid cast in QBE");
            } },
        value.ptype->description, target_type->description);
}

GenResult variable_decl(ASTNode const &n, std::wstring const &name, pType const &type, ASTNode const &init, QBEContext &ctx)
{
    auto const &binding = ctx.add(name, type);
    auto        var_ref = ILValue::variable(binding.index, ILBaseType::L);
    QBEOperand  operand { n, var_ref, type };
    if (init != nullptr) {
        return assign(operand, init, ctx);
    }
    return operand;
}

GenResult global_decl(ASTNode const &n, std::wstring const &name, pType const &type, ASTNode const &init, QBEContext &ctx)
{
    auto init_value = TRY_GENERATE(init, ctx);
    init_value = TRY_DEREFERENCE(init_value, ctx);
    ctx.add_global(name, type, init_value.get_value());
    return QBEOperand { n, ILValue::global(name, ILBaseType::L), type };
}

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

}
