#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

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
        auto &file { ctx.program.files[ctx.current_file] };
        auto &function { file.functions[ctx.current_function] };
        TRY_GENERATE(impl.declaration, ctx);
        TRY_GENERATE(impl.implementation, ctx);
        ctx += RetDef { basetype(qbe_type(function.return_type)) };
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

}
