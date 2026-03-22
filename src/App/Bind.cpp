/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <expected>
#include <ranges>
#include <string>

#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/QBE/QBE.h>

namespace Lia {

#define try_bind(expr)                                                 \
    (                                                                  \
        {                                                              \
            auto const &__expr = (expr);                               \
            if (auto const maybe = bind(__expr); !maybe.has_value()) { \
                return BindError { maybe.error() };                    \
            }                                                          \
            ((__expr)->bound_type);                                    \
        })

#define try_bind_member(m)                            \
    (                                                 \
        {                                             \
            auto      &__m = (m);                     \
            auto const __maybe = bind(__m);           \
            if (!__maybe.has_value()) {               \
                return BindError { __maybe.error() }; \
            }                                         \
            ((__m)->bound_type);                      \
        })

template<typename R>
    requires std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, ASTNode>
static BindResults bind_nodes(R const &nodes)
{
    pTypes                   types;
    std::optional<ASTStatus> ret {};

    for (auto &n : nodes) {
        auto res = bind(n);
        if (!res.has_value() || res.value() == nullptr) {
            if (!ret.has_value()) {
                if (!res.has_value()) {
                    ret = res.error();
                } else {
                    ret = ASTStatus::Undetermined;
                }
            }
            types.push_back(pType { nullptr });
        } else {
            types.push_back(res.value());
        }
    }
    if (ret.has_value()) {
        return std::unexpected(ret.value());
    }
    return types;
}

static BindResults bind_nodes(ASTNodes nodes)
{
    return bind_nodes(nodes | std::ranges::views::all);
}

#define try_bind_nodes(nodes)                                                 \
    (                                                                         \
        {                                                                     \
            auto const &__nodes = (nodes);                                    \
            pTypes      __types;                                              \
            if (auto const maybe = bind_nodes(__nodes); !maybe.has_value()) { \
                return BindError { maybe.error() };                           \
            } else {                                                          \
                __types = maybe.value();                                      \
            }                                                                 \
            (__types);                                                        \
        })

static BindResult bind_declaration(ASTNode declaration, ASTNode definition)
{
    assert(definition->ns != nullptr);
    if (declaration->status == ASTStatus::Normalized) {
        for (auto const &generic_param : get<FunctionDeclaration>(declaration).generics) {
            auto generic_id = get<Identifier>(generic_param);
            if (auto const &generic = definition->ns->find_type(generic_id.identifier); generic != nullptr) {
                assert(is<GenericParameter>(generic));
                continue;
            }
            definition->ns->register_type(generic_id.identifier, TypeRegistry::the().generic_parameter(generic_id.identifier));
        }
    }
    return bind(declaration);
}

static ASTNode instantiate(ASTNode n, std::map<std::wstring, pType> const &generic_args)
{
    assert(n != nullptr);
    assert(is<FunctionDefinition>(n));
    auto const &this_def = get<FunctionDefinition>(n);
    auto       &decl = get<FunctionDeclaration>(this_def.declaration);
    auto       &parser = *(n.repo);

    assert(!decl.generics.empty() && decl.generics.size() == generic_args.size());
    ASTNode new_func = make_node<FunctionDefinition>(n, this_def.name);
    new_func->init_namespace();
    {
        Defer _ { [&n, &parser]() { parser.pop_namespace(n); } };
        for (auto const &[name, type] : generic_args) {
            new_func->ns->register_type(name, TypeRegistry::the().alias_for(type));
        }

        auto &def = get<FunctionDefinition>(new_func);
        def.declaration = make_node<FunctionDeclaration>(
            def.declaration,
            decl.name,
            ASTNodes {},
            stamp(decl.parameters),
            stamp(decl.return_type));
        def.implementation = stamp(this_def.implementation);
    }
    auto _ = bind(new_func);
    // assert(new_func->declaration->bound_type != nullptr && !new_func->declaration->bound_type->is<Undetermined>());
    // std::wcout << "\ninstantiated " << name << ":\n";
    // new_func->dump();
    return new_func;
}

static ASTNode instantiate(ASTNode n, std::vector<pType> const &generic_args)
{
    assert(n != nullptr);
    assert(is<FunctionDefinition>(n));
    auto const &def = get<FunctionDefinition>(n);
    auto       &decl = get<FunctionDeclaration>(def.declaration);
    if (generic_args.size() != decl.generics.size()) {
        n.error("Incompatible number of generic arguments");
        return nullptr;
    }
    std::map<std::wstring, pType> generic_args_map;
    for (auto const &[param, arg] : std::ranges::views::zip(decl.generics, generic_args)) {
        auto param_id = get<Identifier>(param);
        generic_args_map[param_id.identifier] = arg;
    }
    return instantiate(n, generic_args_map);
}

template<class N>
BindResult bind(ASTNode n, N &impl)
{
    return nullptr;
}

template<>
BindResult bind(ASTNode n, BinaryExpression &impl)
{
    assert(n != nullptr);
    Parser &parser = *(n.repo);
    auto    lhs_type = try_bind_member(impl.lhs);

    if (impl.op == Operator::MemberAccess) {
        if (is<TypeType>(lhs_type)) {
            auto type_type = get<TypeType>(lhs_type).type;
            if (!is<Identifier>(impl.rhs)) {
                return parser.bind_error(impl.rhs->location,
                    L"The right-hand side of a member access must be an identifier");
            }
            auto const &label = get<Identifier>(impl.rhs).identifier;
            if (auto res = std::visit(
                    overloads {
                        [&label, &impl, &n, &parser, &type_type](TaggedUnionType const &t) -> std::expected<ASTNode, LiaError> {
                            if (auto v = t.value_for(label); v) {
                                auto ret = make_node<TagValue>(n, static_cast<int64_t>(*v), label, t.payload_for(label), nullptr);
                                ret->bound_type = type_type;
                                ret->status = ASTStatus::Bound;
                                return ret;
                            }
                            return std::unexpected(
                                LiaError {
                                    impl.rhs->location,
                                    std::format(L"Unknown tagged union value `{}`", label),
                                });
                        },
                        [&label, &impl, &n, &parser, &type_type](EnumType const &t) -> std::expected<ASTNode, LiaError> {
                            if (auto v = t.value_for(label); v) {
                                auto ret = make_node<TagValue>(n, static_cast<int64_t>(*v), label, nullptr, nullptr);
                                ret->bound_type = type_type;
                                ret->status = ASTStatus::Bound;
                                return ret;
                            }
                            return std::unexpected(
                                LiaError {
                                    impl.rhs->location,
                                    std::format(L"Unknown enum value `{}`", label),
                                });
                        },
                        [&impl](auto const &) -> std::expected<ASTNode, LiaError> {
                            return std::unexpected(
                                LiaError {
                                    impl.lhs->location,
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
        if (lhs_type->kind() != TypeKind::ReferenceType && !is<Identifier>(impl.lhs)) {
            return parser.bind_error(
                impl.lhs->location,
                L"Left hand side of member access operator must be value reference");
        }
        auto lhs_value_type = lhs_type->value_type();
        return std::visit(
            overloads {
                [&parser, &impl](StructType const &strukt) -> BindResult {
                    auto find_member = [&strukt, &parser, &impl](std::wstring_view const name) -> std::expected<StructType::Field, std::wstring> {
                        for (auto const &field : strukt.fields) {
                            if (field.name == name) {
                                return field;
                            }
                        }
                        return std::unexpected(std::format(L"Unknown struct field `{}`", name));
                    };
                    if (auto struct_field = find_member(get<Identifier>(impl.rhs).identifier); !struct_field) {
                        return parser.bind_error(impl.rhs->location, struct_field.error());
                    } else {
                        impl.rhs->bound_type = TypeRegistry::string;
                        impl.rhs->status = ASTStatus::Bound;
                        return TypeRegistry::the().referencing(struct_field->type);
                    }
                },
                [&parser, &impl, &lhs_value_type, &n](TaggedUnionType const &tagged_union) -> BindResult {
                    if (!is<Identifier>(impl.rhs)) {
                        return parser.bind_error(impl.rhs->location,
                            L"The right-hand side of a member access must be an identifier");
                    }
                    auto const &label = get<Identifier>(impl.rhs).identifier;
                    if (auto v = tagged_union.value_for(label); v) {
                        auto ret = make_node<TagValue>(n, impl.lhs, static_cast<int64_t>(*v), label, tagged_union.payload_for(label), nullptr);
                        return lhs_value_type;
                    }
                    return parser.bind_error(
                        impl.rhs->location,
                        L"Unknown tagged union value `{}`", label);
                },
                [&parser, &lhs_value_type, &impl](auto const &) -> BindResult {
                    return parser.bind_error(
                        impl.rhs->location,
                        L"Left hand side of member access operator has type `{}` which is not a struct", lhs_value_type->name);
                } },
            lhs_value_type->description);
    }

    auto lhs_value_type = lhs_type->value_type();
    auto rhs_type = try_bind_member(impl.rhs);
    auto rhs_value_type = rhs_type->value_type();

    if (impl.op == Operator::Assign) {
        if (is<TagValue>(impl.lhs)) {
            if (!is<TaggedUnionType>(lhs_value_type)) {
                return parser.bind_error(impl.lhs->location, L"Only tagged union values can be assigned a payload");
            }
            auto const &tagged_union { get<TaggedUnionType>(lhs_value_type) };
            auto const &tag_value { get<TagValue>(impl.lhs) };
            if (tag_value.payload != nullptr) {
                return parser.bind_error(impl.rhs->location, L"Can only attach one payload to a tagged value");
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

            auto _ = make_node<TagValue>(
                n,
                tag_value.tag_value,
                tag_value.label,
                tag_value.payload_type,
                impl.rhs);
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

    if (impl.op == Operator::Range) {
        if (lhs_value_type == rhs_value_type
            && (is<IntType>(lhs_value_type) || is<EnumType>(lhs_value_type))) {
            return TypeRegistry::the().range_of(lhs_value_type);
        }
    }

    if (impl.op == Operator::Call && is<FunctionType>(lhs_value_type) && is<TypeList>(rhs_value_type)) {
        fatal("The `Call` binary operator should be elided during the normalization phase");
    }

    if (impl.op == Operator::Cast) {
        if (rhs_value_type->kind() != TypeKind::TypeType) {
            return parser.bind_error(
                impl.rhs->location,
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

    auto check_operators = [&impl](Operator op, pType op_lhs_type, pType op_rhs_type) -> pType {
        for (auto const &o : binary_ops) {
            if (op == o.op && o.matches(impl.lhs, op_lhs_type, op_rhs_type)) {
                return o.return_type(impl.lhs, op_lhs_type, op_rhs_type);
            }
        }
        return nullptr;
    };

    if (auto result = check_operators(impl.op, lhs_value_type, rhs_value_type); result != nullptr) {
        return result;
    }
    if (auto const rhs_coerced_to_lhs = coerce(impl.rhs, lhs_value_type); rhs_coerced_to_lhs != nullptr) {
        if (auto result = check_operators(impl.op, lhs_value_type, rhs_coerced_to_lhs->bound_type); result != nullptr) {
            impl.rhs = rhs_coerced_to_lhs;
            return result;
        }
    }
    if (auto const lhs_coerced_to_rhs = coerce(impl.lhs, rhs_value_type); lhs_coerced_to_rhs != nullptr) {
        if (auto result = check_operators(impl.op, lhs_coerced_to_rhs->bound_type, rhs_value_type); result != nullptr) {
            impl.lhs = lhs_coerced_to_rhs;
            return result;
        }
    }

    return parser.bind_error(
        n->location,
        L"Operator `{}` cannot be applied to left hand type `{}` and right hand type `{}`",
        as_wstring(Operator_name(impl.op)),
        lhs_value_type->name,
        rhs_value_type->name);
}

template<>
BindResult bind(ASTNode n, Block &impl)
{
    auto types = try_bind_nodes(impl.statements);
    if (!types.empty()) {
        return types.back();
    }
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, Break &impl)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, Call &impl)
{
    assert(n != nullptr);
    auto &parser = *(n.repo);
    if (impl.function != nullptr) {
        if (impl.function->status == ASTStatus::Initialized) {
            impl.function = normalize(impl.function);
        }
        try_bind(impl.function);
        auto const &f = get<FunctionDefinition>(impl.function);
        auto const &[_, result] = get<FunctionType>(f.declaration->bound_type);
        return result;
    }

    auto                      arg_types = try_bind(impl.arguments);
    auto const               &type_descr = get<TypeList>(arg_types);
    auto                     &args { get<ExpressionList>(impl.arguments).expressions };
    std::wstring              name;
    std::vector<std::wstring> names;
    ASTNodes                  type_args {};
    if (auto const *id = get_if<Identifier>(impl.callable); id != nullptr) {
        name = id->identifier;
        names = { name };
    } else if (auto const &stamped_id = get_if<StampedIdentifier>(impl.callable); stamped_id != nullptr) {
        name = stamped_id->identifier;
        type_args = stamped_id->arguments;
        names = { name };
    } else if (is<ExpressionList>(impl.callable)) {
        auto const &callable { get<ExpressionList>(impl.callable) };
        std::ranges::for_each(
            callable.expressions | std::views::enumerate,
            [&name, &names](auto const &expr) {
                auto const &[ix, id] = expr;
                if (ix > 0) {
                    name += '.';
                }
                auto &n { get<Identifier>(id).identifier };
                name += n;
                names.emplace_back(n);
            });
    }

    // function = parser.find_function_by_arg_list(id->identifier, arg_types);
    // if (function != nullptr) {
    //     return std::get<FunctionType>(function->bound_type->description).result;
    // }
    //

    auto match_non_generic_function = [&](ASTNode func_def) -> std::expected<ASTNode, ASTStatus> {
        auto const &def = get<FunctionDefinition>(func_def);
        auto const &decl = get<FunctionDeclaration>(def.declaration);
        if (decl.generics.empty()) {
            auto const &func_type_descr = get<FunctionType>(def.declaration->bound_type);
            if (type_descr.types.size() != func_type_descr.parameters.size()) {
                return nullptr;
            }
            ASTNodes reference_nodes;
            auto     made_reference_node { false };
            for (auto [ix, arg_param] : std::views::zip(args, type_descr.types, func_type_descr.parameters) | std::ranges::views::enumerate) {
                auto [arg, arg_type, param_type] { arg_param };
                auto arg_value = arg_type->value_type();
                auto param_value = param_type->value_type();
                if (arg_value != param_value) {
                    return nullptr;
                }
                if (is<ReferenceType>(param_type) && is_constant(arg)) {
                    return nullptr;
                }
                if (is<ReferenceType>(param_type) && !is<ReferenceType>(arg_type)) {
                    auto reference = normalize(parser.make_node<UnaryExpression>(Operator::AddressOf, arg));
                    reference_nodes.emplace_back(reference);
                    made_reference_node = true;
                } else {
                    reference_nodes.emplace_back(arg);
                }
            }
            if (made_reference_node) {
                impl.arguments = normalize(make_node<ExpressionList>(impl.arguments, reference_nodes));
                try_bind(impl.arguments);
            }
            return func_def;
        }
        return nullptr;
    };

    auto match_generic_function = [&](ASTNode func_def) -> std::expected<ASTNode, ASTStatus> {
        auto const &def = get<FunctionDefinition>(func_def);
        auto const &decl = get<FunctionDeclaration>(def.declaration);
        if (!decl.generics.empty()) {
            if (def.declaration->bound_type == nullptr) {
                if (auto const &decl_type = bind_declaration(def.declaration, func_def); !decl_type.has_value()) {
                    return BindError { decl_type.error() };
                }
            }
            std::map<std::wstring, pType> generic_args;
            for (auto const &[param_name, arg_type] : std::ranges::views::zip(decl.generics, type_args)) {
                generic_args[std::wstring(identifier(param_name))] = resolve(arg_type);
            }
            if (decl.parameters.size() != args.size()) {
                return nullptr;
            }
            for (auto const &[param, arg] : std::ranges::views::zip(decl.parameters, args)) {
                auto const &param_type = param->bound_type;
                auto const &arg_type = arg->bound_type;
                for (auto const inferred = arg_type->infer_generic_arguments(param_type); auto const &[name, arg_type] : inferred) {
                    if (generic_args.contains(name) && generic_args[name] != arg_type) {
                        return n.bind_error(
                            L"Ambiguous values inferred for generic parameter  `{}`: `{}` and `{}`",
                            name, arg_types->to_string(), generic_args[name]->to_string());
                    }
                    generic_args[name] = arg_type;
                }
            }
            auto all_inferred { true };
            for (auto const &generic_param : decl.generics) {
                if (!generic_args.contains(std::wstring(identifier(generic_param)))) {
                    all_inferred = false;
                    break;
                }
            }
            if (!all_inferred) {
                return nullptr;
            }
            return instantiate(func_def, generic_args);
        }
        return nullptr;
    };

    ASTNodes bound_overloads;
    {
        auto const overloads = parser.find_overloads(names, type_args);
        for (auto func_def : overloads) {
            if (func_def->status == ASTStatus::Initialized) {
                func_def = normalize(func_def);
            }
            auto const _ = bind(func_def);
            bound_overloads.push_back(func_def);
        }
    }
    if (type_args.empty()) {
        for (auto const &func_def : bound_overloads) {
            auto const &def = get<FunctionDefinition>(func_def);
            auto const &decl = get<FunctionDeclaration>(def.declaration);
            if (auto const ret = match_non_generic_function(func_def); !ret.has_value()) {
                return BindError { ret.error() };
            } else if (ret.value() != nullptr) {
                impl.function = ret.value();
                break;
            }
        }
        if (impl.function == nullptr) {
            for (auto const &func_def : bound_overloads) {
                auto const &def = get<FunctionDefinition>(func_def);
                auto const &decl = get<FunctionDeclaration>(def.declaration);
                if (auto const ret = match_generic_function(func_def); !ret.has_value()) {
                    return BindError { ret.error() };
                } else if (ret.value() != nullptr) {
                    impl.function = ret.value();
                    break;
                }
            }
        }
    } else {
        for (auto const &func_def : bound_overloads) {
            auto const &def = get<FunctionDefinition>(func_def);
            auto const &decl = get<FunctionDeclaration>(def.declaration);
            if (auto const ret = match_generic_function(func_def); !ret.has_value()) {
                return BindError { ret.error() };
            } else if (ret.value() != nullptr) {
                impl.function = ret.value();
                break;
            }
        }
    }
    if (impl.function != nullptr) {
        auto const &func { get<FunctionDefinition>(impl.function) };
        auto const &func_type_descr { get<FunctionType>(func.declaration->bound_type) };
        auto const &param_types { func_type_descr.parameters };
        for (auto const &[arg, param_type] : std::ranges::views::zip(args, param_types)) {
        }
        return func_type_descr.result;
    }
    if (parser.pass == 0) {
        return BindError { ASTStatus::Undetermined };
    }
    return n.bind_error(L"Unresolved function `{}{}`", name, arg_types->to_string());
}

template<>
BindResult bind(ASTNode n, Comptime &impl)
{
    auto &parser = *(n.repo);
    if (n->bound_type == nullptr) {
        if (auto res = parser.bind(impl.statements); !res) {
            return res;
        } else {
            switch (impl.statements->status) {
            case ASTStatus::InternalError:
                log_error("Internal error(s) encountered during compilation of @comptime block");
                return nullptr;
            case ASTStatus::BindErrors:
            case ASTStatus::Ambiguous: {
                log_error("Error(s) found during compilation of @comptime block:");
                for (auto const &err : parser.errors) {
                    log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
                }
                return parser.bind_error(n->location, L"Bind error in @comptime block");
            }
            case ASTStatus::Undetermined:
                return BindError { ASTStatus::Undetermined };
            case ASTStatus::Initialized:
            case ASTStatus::Normalized:
                UNREACHABLE();
            case ASTStatus::Bound:
                trace(L"Comptime script bind successful");
                break;
            }
            trace("Bound compile time script");
        }
    }

    if (impl.output.empty()) {
        if (auto res = QBE::generate_qbe(impl.statements); !res.has_value()) {
            return parser.bind_error(n->location, res.error());
        } else {
            auto  program = res.value();
            auto &file = program.files[0];
            auto &function = file.functions[0];
            if (trace_on()) {
                trace("Compile time block IR:");
                std::wcerr << file;
                trace("---------------------------------------------------");
            }
            QBE::VM vm { program };
            if (auto exec_res = execute_qbe(vm, file, function, {}); !res.has_value()) {
                return parser.bind_error(n->location, res.error());
            } else {
                auto const output_val = exec_res.value();
                impl.output = static_cast<std::wstring>(output_val);
                trace("@comptime block executed");
                trace(L"@comptime output: {}", impl.output);
            }
        }
    }

    if (auto parsed_output = parse<Block>(*(n.repo), impl.output); parsed_output) {
        trace("@comptime after parsing");
        if (trace_on()) {
            dump(parsed_output, std::wcerr);
        }
        impl.statements = normalize(parsed_output);
        trace("@comptime after normalizing");
        if (trace_on()) {
            dump(impl.statements, std::wcerr);
        }
        return bind(impl.statements);
    } else {
        log_error("@comptime parse failed");
        for (auto const &err : parser.errors) {
            log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
        }
        return n.bind_error(L"Error(s) parsing result of @comptime block");
    }
    return nullptr;
}

template<>
BindResult bind(ASTNode n, BoolConstant &impl)
{
    return TypeRegistry::boolean;
}

template<>
BindResult bind(ASTNode n, Decimal &impl)
{
    return TypeRegistry::f64;
}

template<>
BindResult bind(ASTNode n, Number &impl)
{
    return std::visit(
        [](auto v) -> pType {
            return type_of<decltype(v)>();
        },
        impl.value);
}

template<>
BindResult bind(ASTNode n, String &impl)
{
    return TypeRegistry::string;
}

template<>
BindResult bind(ASTNode n, CString &impl)
{
    return TypeRegistry::cstring;
}

template<>
BindResult bind(ASTNode n, Continue &impl)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, DeferStatement &impl)
{
    try_bind(impl.statement);
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, Dummy &impl)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, Enum &impl)
{
    assert(n != nullptr);
    auto &parser = *(n.repo);

    auto is_tagged_union = std::any_of(
        impl.values.cbegin(),
        impl.values.cend(),
        [](ASTNode const &v) -> bool { return std::get<EnumValue>(v->node).payload != nullptr; });

    pType underlying_type { nullptr };
    if (impl.underlying_type != nullptr) {
        underlying_type = resolve(impl.underlying_type);
        if (underlying_type == nullptr) {
            return n.bind_error(L"Could not resolve type `{}`", impl.underlying_type);
        }
    }

    pType    enum_type { nullptr };
    EnumType enoom;
    if (underlying_type != nullptr && is<EnumType>(underlying_type)) {
        if (!is_tagged_union) {
            return n.bind_error(L"Attempt to create enum with existing enum `{}` as underlying type", impl.underlying_type);
        }
        enoom = get<EnumType>(underlying_type);
        enum_type = underlying_type;
    } else if (underlying_type == nullptr || is<IntType>(underlying_type)) {
        if (underlying_type == nullptr) {
            underlying_type = TypeRegistry::i32;
        }
        enoom.underlying_type = underlying_type;
        for (auto const &[ix, v] : std::views::enumerate(impl.values)) {
            auto enum_value = get<EnumValue>(v);
            // TODO there are many ways the numbering of enum values can get
            // confused.
            size_t value = (enum_value.value != nullptr) ? get<int64_t>(get<Number>(enum_value.value)) : ix;
            enoom.values.emplace_back(enum_value.label, value);
        }
    } else if (is_tagged_union) {
        return n.bind_error(L"Invalid underlying type `{}` for tagged union", impl.underlying_type);
    } else {
        return n.bind_error(L"Invalid underlying type `{}` for enum", impl.underlying_type);
    }
    if (!is_tagged_union) {
        auto ret = make_type(impl.name, enoom);
        parser.register_type(impl.name, ret);
        return make_type(impl.name, TypeType { .type = ret });
    }
    if (enum_type == nullptr) {
        enum_type = make_type(std::format(L"$enum${}", impl.name), enoom);
    }
    TaggedUnionType tagged_union;
    for (auto const &[ix, v] : std::ranges::views::enumerate(impl.values)) {
        pType payload { TypeRegistry::void_ };
        auto  tagged_value = get<EnumValue>(v);
        if (tagged_value.payload != nullptr) {
            payload = resolve(tagged_value.payload);
            if (payload == nullptr) {
                return n.bind_error(L"Could not resolve type `{}`", to_string(tagged_value.payload));
            }
        }
        auto sz = tagged_union.tags.size();
        for (auto const &ev : enoom.values) {
            if (ev.label == tagged_value.label) {
                tagged_union.tags.emplace_back(ev.value, payload);
                break;
            }
        }
        if (sz == tagged_union.tags.size()) {
            return n.bind_error(L"Unrecognized enum label `{}` in definition of of tagged union", tagged_value.label);
        }
    }
    tagged_union.tag_type = enum_type;
    auto ret = make_type(impl.name, tagged_union);
    parser.register_type(impl.name, ret);
    return make_type(impl.name, TypeType { .type = ret });
}

template<>
BindResult bind(ASTNode n, EnumValue &impl)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, ExpressionList &impl)
{
    return TypeRegistry::the().typelist_of(try_bind_nodes(impl.expressions));
}

template<>
BindResult bind(ASTNode n, ExternLink &impl)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, ForStatement &impl)
{
    try_bind(impl.range_expr);
    if (!is<RangeType>((impl.range_expr)->bound_type)) {
        return n.bind_error(L"`for` loop range expression is a `{}`, not a range", (impl.range_expr)->bound_type);
    }
    return bind(impl.statement);
}

template<>
BindResult bind(ASTNode n, FunctionDeclaration &impl)
{
    return TypeRegistry::the().function_of(
        try_bind_nodes(impl.parameters),
        get<TypeType>(try_bind(impl.return_type)).type);
}

template<>
BindResult bind(ASTNode n, FunctionDefinition &impl)
{
    assert(n != nullptr);
    auto &parser { *(n.repo) };
    bool  register_me = impl.declaration->bound_type == nullptr;
    if (auto maybe { bind_declaration(impl.declaration, n) }; !maybe.has_value()) {
        return BindError { maybe.error() };
    }
    auto t = (impl.declaration)->bound_type;
    assert(is<FunctionType>(t));
    if (register_me) {
        if (auto const &found = n->ns->parent->find_function(impl.name, t); found != nullptr && found != n) {
            return n.bind_error(L"Duplicate overload for function `{}` with type `{}`", impl.name, t);
        }
        n->ns->parent->register_function(impl.name, n);
    }
    auto &decl = get<FunctionDeclaration>(impl.declaration);
    if (!decl.generics.empty()) {
        return TypeRegistry::void_;
    }
    for (auto const &param : decl.parameters) {
        auto parameter = get<Parameter>(param);
        if (!parser.has_variable(parameter.name)) {
            parser.register_variable(parameter.name, param);
        }
    }
    if (auto impl_type = bind(impl.implementation); impl.implementation->status != ASTStatus::Bound) {
        return impl_type;
    }
    return TypeRegistry::void_;
}

template<class N>
    requires std::is_same_v<N, Identifier> || std::is_same_v<N, StampedIdentifier>
BindResult bind(ASTNode n, N &impl)
{
    auto const &type = n.repo->type_of(impl.identifier);
    if (type == nullptr) {
        if (n.repo->pass == 0) {
            return BindError { ASTStatus::Undetermined };
        } else {
            return n.bind_error(L"Unresolved identifier `{}`", impl.identifier);
        }
    }
    return type;
}

template<>
BindResult bind(ASTNode n, IfStatement &impl)
{
    try_bind(impl.condition);
    if (!impl.condition->bound_type->assignable_to(TypeRegistry::boolean)) {
        return n.bind_error(
            L"`if` loop condition is a `{}`, not a boolean",
            (impl.condition)->bound_type->name);
    }
    try_bind(impl.if_branch);
    if (impl.else_branch != nullptr) {
        try_bind(impl.else_branch);
    }
    auto if_type = (impl.if_branch)->bound_type;
    if (impl.else_branch == nullptr || (impl.else_branch)->bound_type == if_type) {
        return if_type;
    }
    return BindError { ASTStatus::Ambiguous };
}

template<>
BindResult bind(ASTNode n, LoopStatement &impl)
{
    return bind(impl.statement);
}

template<>
BindResult bind(ASTNode n, Module &impl)
{
    try_bind_nodes(impl.statements);
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode, Nullptr &)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, Parameter &impl)
{
    return get<TypeType>(try_bind(impl.type_name)).type;
}

template<>
BindResult bind(ASTNode n, Program &impl)
{
    assert(n != nullptr);
    auto &parser { *(n.repo) };
    pType ret { nullptr };
    if (parser.pass == 0) {
        for (auto &[name, mod] : parser.modules) {
            n->ns->register_variable(name, mod);
        }
    }
    try_bind_nodes(impl.statements);
    try_bind_nodes(parser.modules | std::ranges::views::values);
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, PublicDeclaration &impl)
{
    return bind(impl.declaration);
}

template<>
BindResult bind(ASTNode n, Return &impl)
{
    try_bind(impl.expression);
    auto &parser { *(n.repo) };
    auto  func = parser.current_function();
    assert(func != nullptr && is<FunctionDefinition>(func));
    auto func_signature = get<FunctionDefinition>(func).declaration->bound_type;
    assert(is<FunctionType>(func_signature));
    auto return_type = get<FunctionType>(func_signature).result;
    if (impl.expression != nullptr && !impl.expression->bound_type->assignable_to(return_type)) {
        return n.bind_error(
            L"`return` expression is a `{}` and is not assignable to a `{}`",
            (impl.expression)->bound_type->name, return_type->name);
    }
    if (impl.expression == nullptr && return_type != TypeRegistry::void_) {
        return n.bind_error(
            L"`return` requires an expression of type `{}`",
            return_type->name);
    }
    if (impl.expression != nullptr && return_type == TypeRegistry::void_) {
        return n.bind_error(
            L"`return` returns from a function returning `void` and therefore cannot return a value");
    }
    return return_type;
}

template<>
BindResult bind(ASTNode n, Struct &impl)
{
    assert(n != nullptr);
    auto              &parser { *(n.repo) };
    StructType::Fields fields;
    for (auto const &m : impl.members) {
        try_bind(m);
        fields.emplace_back(get<StructMember>(m).label, m->bound_type);
    }
    pType type = TypeRegistry::the().struct_of(fields);
    parser.register_type(impl.name, type);
    return make_type(impl.name, TypeType { .type = type });
}

template<>
BindResult bind(ASTNode n, StructMember &impl)
{
    return get<TypeType>(try_bind(impl.member_type)).type;
}

template<>
BindResult bind(ASTNode n, TypeSpecification &impl)
{
    auto ret = resolve(n);
    if (ret == nullptr) {
        return BindError { ASTStatus::Undetermined };
    }
    return TypeRegistry::the().type_of(ret);
}

template<>
BindResult bind(ASTNode n, UnaryExpression &impl)
{
    assert(n != nullptr);
    auto &parser { *(n.repo) };
    auto  operand_type = try_bind(impl.operand);
    if (impl.op == Operator::Sizeof) {
        return TypeRegistry::i64;
    }
    if (impl.op == Operator::AddressOf) {
        if (is<ReferenceType>(operand_type)) {
            return parser.bind_error(n->location, L"Cannot take address of reference");
        }
        if (is_constant(n)) {
            return parser.bind_error(n->location, L"Cannot take address of constant");
        }
        return TypeRegistry::the().referencing(operand_type);
    }
    for (auto const &[oper, operand, result] : unary_ops) {
        if (impl.op == oper && operand.matches(impl.operand, operand_type)) {
            return std::visit(
                overloads {
                    [](TypeKind const &result_type) -> pType {
                        UNREACHABLE();
                    },
                    [&operand_type, &operand, &impl](PseudoType const &pseudo_type) -> pType {
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
                                    [&impl](TaggedUnionType const &result) -> pType {
                                        assert(impl.operand->type() == SyntaxNodeType::TagValue);
                                        return TypeRegistry::the().referencing(get<TagValue>(impl.operand).payload_type);
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
        as_wstring(Operator_name(impl.op)),
        operand_type->name);
}

template<>
BindResult bind(ASTNode n, VariableDeclaration &impl)
{
    auto  my_type = (impl.type_name != nullptr) ? try_bind(impl.type_name) : nullptr;
    auto  init_type = (impl.initializer != nullptr) ? try_bind(impl.initializer) : nullptr;
    auto &parser = *(n.repo);

    assert(my_type != nullptr || init_type != nullptr);

    if (my_type == nullptr) {
        my_type = init_type;
    } else {
        my_type = get<TypeType>(my_type).type;
    }
    if (init_type != nullptr && !init_type->compatible(my_type) && !init_type->assignable_to(my_type)) {
        return n.bind_error(
            L"Type mismatch between declared type `{}` of `{}` and type of initializer value `{}`",
            my_type->name, impl.name, init_type->name);
    }
    if (n.repo->has_variable(impl.name)) {
        return n.bind_error(L"Duplicate variable name `{}`", impl.name);
    }
    parser.register_variable(impl.name, n);
    return my_type;
}

template<>
BindResult bind(ASTNode, Void &)
{
    return TypeRegistry::void_;
}

template<>
BindResult bind(ASTNode n, WhileStatement &impl)
{
    try_bind(impl.condition);
    pType t = (impl.condition)->bound_type;
    if (!is<BoolType>(t)) {
        return n.bind_error(L"`while` loop condition is a `{}`, not a boolean", t->name);
    }
    return bind(impl.statement);
}

template<>
BindResult bind(ASTNode n, Yield &impl)
{
    return bind(impl.statement);
}

/* ======================================================================== */

BindResult bind(ASTNode node)
{
    assert(node != nullptr);
    Parser &parser = *(node.repo);
    assert(node->status >= ASTStatus::Normalized);
    if (node->status == ASTStatus::Bound) {
        return node->bound_type;
    } else if (node->status > ASTStatus::Bound) {
        return BindError { node->status };
    } else { // node->status < ASTStatus::Bound) {
        size_t stack_size = parser.namespaces.size();
        if (node->ns != nullptr) {
            parser.push_namespace(node);
        }
        parser.node_stack.emplace_back(node);
        auto retval = std::visit(
            [&node](auto impl) {
                auto id { node->id.id.value() };
                // We take the impl by value so we can update the copied
                // value in the bind but at the same time be able to create
                // new nodes and potentially move the nodes vector around.
                auto ret = bind(node, impl);

                // Only copy the impl back if the node is, you know,
                // still the node and hasn't been superceded in the bind
                if (id == node->id.id.value()) {
                    node->node = impl;
                }
                return ret;
            },
            node->node);
        parser.node_stack.pop_back();
        while (stack_size < parser.namespaces.size()) {
            parser.pop_namespace(node);
        }
        if (retval.has_value()) {
            auto const &type = retval.value();
            if (type == nullptr) {
                node->status = ASTStatus::Undetermined;
                parser.unbound_nodes.push_back(node);
                parser.unbound++;
                return BindError { ASTStatus::Undetermined };
            } else {
                node->bound_type = type;
                node->status = ASTStatus::Bound;
                return type;
            }
        } else {
            switch (retval.error()) {
            case ASTStatus::InternalError:
                dump(node, std::wcerr);
                assert("bind(): Internal error" == nullptr);
                break;
            case ASTStatus::Undetermined:
                parser.unbound_nodes.push_back(node);
                parser.unbound++;
                /* Fallthrough */
            default:
                node->status = retval.error();
                break;
            }
            return retval;
        }
    }
}
}
