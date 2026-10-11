#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

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

    trace(QBE, L"Generating call to `{}`", impl.callable);
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
        trace(QBE, L"  Argument `{}`: {} param type {}", get<Parameter>(param).name, arg->bound_type->name, param->bound_type->name);
        if (is<ReferenceType>(param->bound_type)) {
            trace(QBE, "param is reference");
            QBE_ASSERT(is<ReferenceType>(arg->bound_type), ctx);
            auto value = TRY_GENERATE(arg, ctx).get_value();
            value.type = ILBaseType::L;
            args.emplace_back(value);
        } else if (is<ReferenceType>(arg->bound_type) && !qbe_first_class_type(param->bound_type)) {
            trace(QBE, "param not reference, arg is reference");
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
            trace(QBE, "param not reference, arg not reference");
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

}
