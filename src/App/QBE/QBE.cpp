/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ranges>
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

GenResult  assign(QBEOperand const &lhs, ASTNode const &rhs, QBEContext &ctx);
GenResult  cast(QBEOperand value, pType const &target_type, QBEContext &ctx);
GenResult  variable_decl(ASTNode const &n, std::wstring const &name, pType const &type, ASTNode const &init, QBEContext &ctx);
GenResult  global_decl(ASTNode const &n, std::wstring const &name, pType const &type, ASTNode const &init, QBEContext &ctx);
GenBinExpr make_binexpr(ASTNode const &n, QBEOperand lhs, Operator op, QBEOperand rhs, QBEContext &ctx);

GenResult generate_qbe_nodes(ASTNodes const &nodes, QBEContext &ctx)
{
    QBEOperand operand;
    for (auto const &n : nodes) {
        operand = TRY_GENERATE(n, ctx);
    }
    return operand;
}

template<class Node>
GenResult generate_qbe_node(ASTNode const &, Node const &impl, QBEContext &)
{
    NYI(L"Unimplemented QBE serialization for {}", demangle<decltype(impl)>());
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Alias const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, ArgumentList const &impl, QBEContext &ctx)
{
    ILValues values;
    int      align = 0;
    int      size = 0;
    for (auto const &expr : impl.arguments) {
        QBEOperand operand { TRY_GENERATE(expr, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
        auto const &type = values.back().type;
        if (size > 0) {
            size = alignat(size, align_of(type));
        }
        size += size_of(type);
        align = std::max(align, align_of(type));
    }
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, BinaryExpression const &impl, QBEContext &ctx)
{
    assert(impl.lhs->bound_type != nullptr);
    assert(impl.rhs->bound_type != nullptr);
    trace(L"Generating BinExpr {} {} {}", impl.lhs->bound_type->name, as_wstring(Operator_name(impl.op)), impl.rhs->bound_type->name);

    if (impl.op == Operator::Assign) {
        return assign(TRY_GENERATE(impl.lhs, ctx), impl.rhs, ctx);
    }
    QBEOperand lhs_operand { impl.lhs };
    QBEOperand rhs_operand { impl.rhs };
    auto       res = make_binexpr(n, lhs_operand, impl.op, rhs_operand, ctx);
    if (!res) {
        return std::unexpected(res.error());
    }
    trace(L"binexpr {} {} {}", res.value().lhs.get_value(), as_wstring(Operator_name(res.value().op)), res.value().rhs.get_value());
    return qbe_operator(res.value(), ctx);
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Block const &impl, QBEContext &ctx)
{
    if (ctx.program.name.empty()) {
        ctx.program.name = std::format(L"anonymous-{}", n.id.value_or(0));
    }
    if (ctx.program.files.empty()) {
        auto &file = ctx.program.files.emplace_back(ctx.program.name);
        file.id = ctx.program.files.size() - 1;
        ctx.current_file = file.id;
        ctx.current_function = 0;
    }
    auto &file = ctx.program.files[ctx.current_file];
    if (ctx.current_function >= file.functions.size()) {
        ctx.add_function(file.name, n->bound_type);
    }
    ctx.add_operation(
        LabelDef {
            ++ctx.next_label,
        });

    for (auto const &s : impl.statements) {
        if (auto res = generate_qbe_node(s, ctx); !res) {
            return res;
        }
    }
    return { };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Call const &impl, QBEContext &ctx)
{
    auto def = get<FunctionDefinition>(impl.function);
    auto decl = get<FunctionDeclaration>(def.declaration);
    auto name { std::visit(
        overloads {
            [](ExternLink const &link) -> std::wstring {
                return link.link_name;
            },
            [&def, &impl](auto const &) -> std::wstring {
                return def.mangled_name(impl.function);
            } },
        def.implementation->node) };
    ILValue              ret_val { ILValue::null() };
    ILValue              ret_alloc { ILValue::null() };
    size_t               alloc_sz { 0 };
    std::vector<ILValue> args;

    trace(L"Generating call to `{}`", impl.callable);
    if (n->bound_type != TypeRegistry::void_) {
        alloc_sz = std::visit(
            overloads {
                [](BoolType const &) -> size_t {
                    return 0;
                },
                [](IntType const &) -> size_t {
                    return 0;
                },
                [](FloatType const &) -> size_t {
                    return 0;
                },
                [&n](auto const &) -> size_t {
                    return n->bound_type->size_of();
                } },
            n->bound_type->description);
        ret_val = ILValue::local(++ctx.next_var, ctx.qbe_type(n->bound_type));
    }

    if (alloc_sz > 0) {
        auto temp { ctx.add_temporary(n->bound_type) };
        ret_alloc = ILValue::temporary(temp.index, temp.type);
    }

    for (auto const &[arg, param] : std::ranges::views::zip(get<ArgumentList>(impl.arguments).arguments, decl.parameters)) {
        trace(L"  Argument `{}`: {} param type {}", get<Parameter>(param).name, arg->bound_type->name, param->bound_type->name);
        if (is<ReferenceType>(param->bound_type)) {
            trace("param is reference");
            QBE_ASSERT(is<ReferenceType>(arg->bound_type), ctx);
            auto value = TRY_GENERATE(arg, ctx).get_value();
            value.type = ILBaseType::L;
            args.emplace_back(value);
        } else if (is<ReferenceType>(arg->bound_type) && !qbe_first_class_type(param->bound_type)) {
            trace("param not reference, arg is reference");
            auto temp { ctx.add_temporary(param->bound_type) };
            auto arg_alloc { ILValue::temporary(temp.index, param->bound_type) };
            auto ptr { ILValue::local(++ctx.next_var, ILBaseType::L) };
            ctx.add_operation(
                LoadDef {
                    TRY_GENERATE(arg, ctx).get_value(),
                    ptr,
                });
            ctx.add_operation(
                BlitDef {
                    ptr,
                    arg_alloc,
                    param->bound_type->size_of(),
                });
            args.emplace_back(arg_alloc);
        } else {
            trace("param not reference, arg not reference");
            auto operand { TRY_GENERATE(arg, ctx) };
            auto arg_value { TRY_DEREFERENCE(operand, ctx) };
            auto materialized { TRY_MATERIALIZE(arg_value, ctx) };

            std::visit(
                overloads {
                    [&materialized, &ctx, &args](ILBaseType const bt) -> void {
                        if (size_of(bt) < 4) {
                            auto word { ILValue::local(++ctx.next_var, ILBaseType::W) };
                            ctx.add_operation(
                                ExtDef {
                                    materialized.get_value(),
                                    word,
                                });
                            args.emplace_back(word);
                        } else {
                            args.emplace_back(materialized.get_value());
                        }
                    },
                    [&materialized, &args](auto const) -> void {
                        args.emplace_back(materialized.get_value());
                    } },
                materialized.get_value().type.inner);
        }
    }

    ctx.add_operation(
        CallDef {
            std::wstring { name },
            ret_val,
            args,
        });
    if (alloc_sz == 0) {
        return QBEOperand { n, ret_val };
    }
    if (alloc_sz <= 8) {
        ctx.add_operation(StoreDef {
            ret_val,
            ret_alloc,
        });
    } else {
        ctx.add_operation(BlitDef {
            .src = ret_val,
            .dest = ret_alloc,
            .bytes = static_cast<intptr_t>(alloc_sz),
        });
    }
    return QBEOperand { n, ret_alloc };
}

template<>
GenResult generate_qbe_node(ASTNode const &, Comptime const &impl, QBEContext &ctx)
{
    return generate_qbe_node(impl.statements, ctx);
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Void const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, BoolConstant const &impl, QBEContext &)
{
    return QBEOperand { n, ILValue::integer(impl.value ? 1 : 0, ILBaseType::W) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Number const &impl, QBEContext &)
{
    auto const &int_type { get<IntType>(n->bound_type) };
    ILBaseType  t;
    int64_t     v;
    switch (int_type.width_bits) {
    case 8:
        t = (int_type.is_signed) ? ILBaseType::SB : ILBaseType::UB;
        v = (int_type.is_signed) ? get<int8_t>(impl) : get<uint8_t>(impl.value);
        break;
    case 16:
        t = (int_type.is_signed) ? ILBaseType::SH : ILBaseType::UH;
        v = (int_type.is_signed) ? get<int16_t>(impl) : get<uint16_t>(impl.value);
        break;
    case 32:
        t = (int_type.is_signed) ? ILBaseType::SW : ILBaseType::UW;
        v = (int_type.is_signed) ? get<int32_t>(impl.value) : get<uint32_t>(impl.value);
        break;
    case 64:
        t = ILBaseType::L;
        v = (int_type.is_signed) ? get<int64_t>(impl.value) : get<uint64_t>(impl.value);
        break;
    }
    return QBEOperand { n, ILValue::integer(v, t) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Decimal const &impl, QBEContext &)
{
    auto const &float_type { get<FloatType>(n->bound_type) };
    ILBaseType  t;
    double      dbl;
    switch (float_type.width_bits) {
    case 32:
        t = ILBaseType::S;
        dbl = static_cast<float>(impl.value);
        break;
    case 64:
        t = ILBaseType::D;
        dbl = impl.value;
        break;
    }
    return QBEOperand { n, ILValue::float_val(dbl, t) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, CString const &impl, QBEContext &ctx)
{
    return QBEOperand { n, ctx.add_cstring(static_cast<char const *>(impl.string.data())) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, String const &impl, QBEContext &ctx)
{
    auto len_id { ++ctx.next_var };
    auto temp { ctx.add_temporary(TypeRegistry::string) };
    auto ret { ILValue::temporary(temp.index, TypeRegistry::string) };
    ctx.add_operation(
        StoreDef {
            ctx.add_string(impl.string),
            ret,
        });
    ctx.add_operation(
        ExprDef {
            ret,
            ILValue::integer(8, ILBaseType::L),
            ILOperation::Add,
            ILValue::pointer(len_id),
        });
    ctx.add_operation(
        StoreDef {
            ILValue::integer(impl.string.length(), ILBaseType::L),
            ILValue::pointer(len_id),
        });
    return QBEOperand { n, ret };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Dummy const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Nullptr const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Enum const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, ExpressionList const &impl, QBEContext &ctx)
{
    ILValues values;
    for (auto const &expr : impl.expressions) {
        QBEOperand operand { TRY_GENERATE(expr, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
    }
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Extern const &impl, QBEContext &ctx)
{
    if (!impl.library.empty()) {
        ctx.program.libraries.emplace_back(impl.library);
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, ForStatement const &impl, QBEContext &ctx)
{
    auto        top_loop = ++ctx.next_label;
    auto        cont_loop = ++ctx.next_label;
    auto        end_loop = ++ctx.next_label;
    auto const &range = get<BinaryExpression>(impl.range_expr);
    auto const &range_start = range.lhs;
    auto const &range_end = TRY_GENERATE(range.rhs, ctx);
    if (auto res = variable_decl(n, impl.range_variable, range_start->bound_type, range_start, ctx); !res.has_value()) {
        return std::unexpected(res.error());
    } else {
        auto const &range_var = res.value();
        ctx.add_operation(LabelDef { top_loop });
        auto range_var_value_pre = ILValue::local(++ctx.next_var, qbe_type(range_start->bound_type));
        ctx.add_operation(
            LoadDef {
                range_var.get_value(),
                range_var_value_pre,
            });
        auto range_ended = ILValue::local(++ctx.next_var, ILBaseType::W);
        ctx.add_operation(
            ExprDef {
                range_var_value_pre,
                range_end.get_value(),
                ILOperation::Less,
                range_ended,
            });
        ctx.add_operation(
            JnzDef {
                range_ended,
                cont_loop,
                end_loop,
            });
        ctx.add_operation(LabelDef { cont_loop });
        TRY_GENERATE(impl.statement, ctx);
        auto range_var_value_post = ILValue::local(++ctx.next_var, qbe_type(range_start->bound_type));
        ctx.add_operation(
            LoadDef {
                range_var.get_value(),
                range_var_value_post,
            });
        auto range_var_value_inc = ILValue::local(++ctx.next_var, qbe_type(range_start->bound_type));
        ctx.add_operation(
            ExprDef {
                range_var_value_post,
                ILValue::integer(1, range_var.get_value().type),
                ILOperation::Add,
                range_var_value_inc,
            });
        ctx.add_operation(
            StoreDef {
                range_var_value_inc,
                range_var.get_value(),
            });
        ctx.add_operation(JmpDef { top_loop });
        ctx.add_operation(LabelDef { end_loop });
        return QBEOperand { n, ILValue::null() };
    }
}

template<>
GenResult generate_qbe_node(ASTNode const &n, FunctionDefinition const &impl, QBEContext &ctx)
{
    if (ctx.program.name.empty()) {
        ctx.program.name = std::format(L"anonymous-{}", n.id.value_or(0));
    }
    if (ctx.program.files.empty()) {
        auto &file = ctx.program.files.emplace_back(ctx.program.name);
        file.id = ctx.program.files.size() - 1;
        ctx.current_file = file.id;
        ctx.current_function = 0;
    }
    if (!is<ExternLink>(impl.implementation)) {
        ctx.is_export = impl.visibility != Visibility::Static;
        ctx.add_function(impl.mangled_name(n), get<FunctionType>(n->bound_type).result);
        TRY_GENERATE(impl.declaration, ctx);
        TRY_GENERATE(impl.implementation, ctx);
        ctx.is_export = false;
        ctx.current_function = ctx.program.files[ctx.current_file].functions.size();
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, FunctionDeclaration const &impl, QBEContext &ctx)
{
    trace(L"Generating function `{}` -> {}", impl.name, get<TypeType>(impl.return_type->bound_type).type->to_string());
    auto _ = generate_qbe_nodes(impl.parameters, ctx);
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Identifier const &impl, QBEContext &ctx)
{
    assert(n->bound_type != nullptr);
    assert(n->bound_type.repo != nullptr);
    auto t = ctx.qbe_type(n->bound_type);
    if (is<ReferenceType>(n->bound_type)) {
        for (auto const &binding : ctx.function().parameters) {
            if (binding.name == impl.identifier && !binding.var_index) {
                return QBEOperand { n, ILValue::parameter(binding.param_index, t) };
            }
        }
    }
    for (auto const &binding : ctx.function().variables) {
        if (binding.name == impl.identifier) {
            return QBEOperand { n, ILValue::variable(binding.index, t) };
        }
    }
    for (auto const &binding : ctx.file().globals) {
        if (binding.name == impl.identifier) {
            return QBEOperand { n, ILValue::global(binding.name, t) };
        }
    }
    fatal(L"Could not find variable `{}`", impl.identifier);
}

template<>
GenResult generate_qbe_node(ASTNode const &n, IfStatement const &impl, QBEContext &ctx)
{
    auto if_block = ++ctx.next_label;
    auto after_if = ++ctx.next_label;
    int  else_block = 0;
    auto cond_false = after_if;
    if (impl.else_branch != nullptr) {
        else_block = ++ctx.next_label;
        cond_false = else_block;
    }
    auto condition_var = TRY_GENERATE(impl.condition, ctx);
    if (impl.condition->bound_type != TypeRegistry::boolean) {
        if (auto res = cast(condition_var, TypeRegistry::boolean, ctx); !res.has_value()) {
            return res;
        } else {
            condition_var = res.value();
        }
    }
    ctx.add_operation(
        JnzDef {
            condition_var.get_value(),
            if_block,
            cond_false,
        });
    ctx.add_operation(LabelDef { if_block });
    TRY_GENERATE(impl.if_branch, ctx);
    if (impl.else_branch != nullptr) {
        ctx.add_operation(JmpDef { after_if });
        ctx.add_operation(LabelDef { else_block });
        TRY_GENERATE(impl.else_branch, ctx);
    }
    ctx.add_operation(LabelDef { after_if });
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Module const &impl, QBEContext &ctx)
{
    auto &file = ctx.program.files.emplace_back(impl.name);
    file.id = ctx.program.files.size() - 1;
    ctx.current_file = file.id;
    ctx.current_function = std::numeric_limits<size_t>::max();
    if (auto res = generate_qbe_nodes(impl.statements, ctx); !res) {
        return res;
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, ModuleProxy const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Parameter const &impl, QBEContext &ctx)
{
    auto const &param_binding = ctx.add_parameter(impl.name, n->bound_type);
    auto        param = ILValue::parameter(param_binding.param_index, ctx.qbe_type(n->bound_type));

    if (param_binding.var_index) {
        auto const &binding = ctx.add(impl.name, n->bound_type);
        assert(binding.index == *param_binding.var_index);
        auto var = ILValue::variable(*param_binding.var_index, param.type);
        std::visit(
            overloads {
                [&ctx, &param, &var](BoolType const &) {
                    ctx.add_operation(StoreDef { param, var });
                },
                [&ctx, &param, &var](IntType const &) {
                    ctx.add_operation(StoreDef { param, var });
                },
                [&ctx, &param, &var](FloatType const &) {
                    ctx.add_operation(StoreDef { param, var });
                },
                [](std::monostate const &) {
                    UNREACHABLE();
                },
                [&ctx, &param, &var](auto const &descr) {
                    intptr_t sz = static_cast<intptr_t>(descr.size_of());
                    ctx.add_operation(BlitDef { param, var, sz });
                },
            },
            n->bound_type->description);
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Program const &impl, QBEContext &ctx)
{
    ctx.program.name = impl.name;
    auto &parser { *(n.repo) };
    for (auto const &[mod_name, mod] : parser.modules) {
        if (auto res = generate_qbe_node(mod, ctx); !res) {
            return res;
        }
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &, PublicDeclaration const &impl, QBEContext &ctx)
{
    ctx.is_export = true;
    auto ret = generate_qbe_node(impl.declaration, ctx);
    ctx.is_export = false;
    return ret;
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Return const &impl, QBEContext &ctx)
{
    RetDef ret { };
    if (impl.expression != nullptr) {
        auto &file = ctx.program.files[ctx.current_file];
        auto &function = file.functions[ctx.current_function];
        if (function.return_type->size_of() > 8) {
            if (auto res = assign(QBEOperand { n, ILValue::return_value(ILBaseType::L) }, impl.expression, ctx); !res.has_value()) {
                return res;
            };
            ret.expr = ILValue::return_value(ILBaseType::L);
        } else {
            ret.expr = TRY_GENERATE(impl.expression, ctx).get_value();
        }
    }
    ctx.add_operation(ret);
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Struct const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, TagValue const &impl, QBEContext &ctx)
{
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
}

template<>
GenResult generate_qbe_node(ASTNode const &n, VariableDeclaration const &impl, QBEContext &ctx)
{
    if (!is_constant(n)) {
        if (ctx.in_global_scope()) {
            return global_decl(n, impl.name, n->bound_type, impl.initializer, ctx);
        }
        return variable_decl(n, impl.name, n->bound_type, impl.initializer, ctx);
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, UnaryExpression const &impl, QBEContext &ctx)
{
    QBEOperand   operand { TRY_GENERATE(impl.operand, ctx) };
    QBEUnaryExpr expr { n, impl.op, operand };
    auto         var = qbe_operator(expr, ctx);
    return var;
}

template<>
GenResult generate_qbe_node(ASTNode const &n, WhileStatement const &impl, QBEContext &ctx)
{
    auto top_loop = ++ctx.next_label;
    auto cont_loop = ++ctx.next_label;
    auto end_loop = ++ctx.next_label;
    ctx.add_operation(LabelDef { top_loop });
    auto condition_var = TRY_GENERATE(impl.condition, ctx);
    if (impl.condition->bound_type != TypeRegistry::boolean) {
        if (auto res = cast(condition_var, TypeRegistry::boolean, ctx); !res.has_value()) {
            return res;
        } else {
            condition_var = res.value();
        }
    }
    ctx.add_operation(
        JnzDef {
            condition_var.get_value(),
            cont_loop,
            end_loop,
        });
    ctx.add_operation(LabelDef { cont_loop });
    TRY_GENERATE(impl.statement, ctx);
    ctx.add_operation(JmpDef { top_loop });
    ctx.add_operation(LabelDef { end_loop });
    return QBEOperand { n, ILValue::null() };
}

GenResult generate_qbe_node(ASTNode const &n, QBEContext &ctx)
{
    trace("Generating {}", SyntaxNodeType_name(n->type()));
    if (ctx.current_file < ctx.program.files.size()) {
        auto &file { ctx.program.files[ctx.current_file] };
        if (ctx.current_function < file.functions.size() && !is<Dummy>(n)) {
            ctx.add_operation(
                DbgLoc {
                    n->location.line,
                    n->location.column,
                    as_wstring(SyntaxNodeType_name(n->type())),
                });
        }
    }
    return std::visit(
        [&n, &ctx](auto const &impl) -> GenResult {
            return generate_qbe_node(n, impl, ctx);
        },
        n->node);
}

std::expected<ILProgram, std::wstring> generate_qbe(ASTNode const &node)
{
    QBEContext ctx { };
    info("[QBE] Generating IL");
    if (auto res = generate_qbe_node(node, ctx); res.has_value()) {
        info("[QBE] IL generated");
        return ctx.program;
    } else {
        log_error(L"[QBE] Error generating IL: {}", res.error());
        return std::unexpected(res.error());
    }
}

std::expected<void, std::wstring> compile_qbe(ILProgram const &program)
{
    fs::path dot_lia { ".lia" };
    fs::create_directory(dot_lia);
    std::vector<fs::path> o_files;

    for (auto const &file : program.files) {
        if (file.has_exports) {
            fs::path file_path = dot_lia / file.name;
            file_path.replace_extension("ssa");
            {
                fs::path file_path = dot_lia / file.name;
                file_path.replace_extension("ssa");
                std::wofstream os { file_path.string() };
                os << file;
            }
            auto s_file { file_path };
            s_file.replace_extension("s");
            auto o_file { file_path };
            o_file.replace_extension("o");

            info("[QBE] Compiling `{}`", file_path.string());
            Util::Process qbe { "qbe", "-o", s_file.string(), file_path.string() };
            if (auto res = qbe.execute(); !res.has_value()) {
                return std::unexpected(std::format(L"qbe execution failed: {}", as_wstring(res.error().description)));
            } else if (res.value() != 0) {
                return std::unexpected(std::format(L"qbe failed: {}", as_wstring(qbe.stderr())));
            }
            Util::Process as { "as", "-o", o_file.string(), s_file.string() };
            if (auto res = as.execute(); !res.has_value()) {
                return std::unexpected(std::format(L"as execution failed: {}", as_wstring(res.error().description)));
            } else if (res.value() != 0) {
                return std::unexpected(std::format(L"as failed: {}", as_wstring(as.stderr())));
            }
            if (!has_option("keep-assembly")) {
                fs::remove(file_path);
                fs::remove(s_file);
            }
            o_files.push_back(o_file);
        }
    }

    if (!o_files.empty()) {
        fs::path program_path { as_utf8(program.name) };
        program_path.replace_extension();
        info("[QBE] Linking `{}`", program_path.string());

        std::vector<std::string> ld_args {
            "-o",
            fs::path { as_utf8(program.name) }.replace_extension("").string(),
            // "-no-pie",
            std::format("-L{}/lib", Lia::lia_dir().string()),
        };
        for (auto const &o : o_files) {
            ld_args.push_back(o.string());
        }
        for (auto const &lib : program.libraries) {
            auto l { (lib.starts_with(L"lib")) ? lib.substr(3) : lib };
            ld_args.emplace_back(std::format("-l{}", as_utf8(l)));
        }
        if (has_option("L")) {
            for (auto const &lib_path : get_option_values("L")) {
                ld_args.push_back(std::format("-L{}", lib_path));
            }
        }
        if (has_option("l")) {
            for (auto const &lib : get_option_values("l")) {
                ld_args.push_back(std::format("-l{}", lib));
            }
        }
        ld_args.append_range(std::array<std::string, 4> { "-lliart", "-lm", "-lpthread", "-ldl" });

        Util::Process link { "cc", ld_args };
        if (auto res = link.execute(); !res.has_value()) {
            return std::unexpected(std::format(L"Linker execution failed: {}", as_wstring(res.error().description)));
        } else if (res.value() != 0) {
            return std::unexpected(std::format(L"Linking failed: {}", as_wstring(link.stderr())));
        }
        if (!has_option("keep-objects")) {
            for (auto const &o : o_files) {
                fs::remove(o);
            }
        }
    }
    return { };
}
}
