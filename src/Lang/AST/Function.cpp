
/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

#include <Lang/QBE/QBE.h>
#include <ranges>

namespace Lang {

using namespace std::literals;

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
    return Lang::bind(declaration);
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
            ASTNodes { },
            stamp(decl.parameters),
            stamp(decl.return_type));
        def.implementation = stamp(this_def.implementation);
    }
    Lang::bind(new_func);
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

ArgumentList::ArgumentList(ASTNodes arguments)
    : arguments(std::move(arguments))
{
}

ASTNode ArgumentList::normalized(ASTNode const &n) const
{
    return make_node<ArgumentList>(n, normalize(arguments));
}

BindResult ArgumentList::bind(ASTNode const &n) const
{
    return TypeRegistry::the().typelist_of(try_bind_nodes(arguments));
}

Call::Call(ASTNode callable, ASTNode args, ASTNode function)
    : callable(std::move(callable))
    , arguments(std::move(args))
    , function(std::move(function))
{
    if (!is<Identifier>(this->callable) && !is<StampedIdentifier>(this->callable) && !is<IdentifierList>(this->callable)) {
        NYI("Callable must be a function name, not a {}", SyntaxNodeType_name(this->callable->type()));
    }
    assert(this->arguments != nullptr);
}

ASTNode Call::normalized(ASTNode const &n) const
{
    return make_node<Call>(n, normalize(callable), normalize(arguments), normalize(function));
}

BindResult Call::bind(ASTNode const &n) const
{
    assert(n != nullptr);
    auto &parser = *(n.repo);
    if (function != nullptr) {
        auto f { function };
        if (f->status == ASTStatus::Initialized) {
            f = normalize(f);
        }
        try_bind(f);
        auto const &def = get<FunctionDefinition>(f);
        auto const &[_, result] = get<FunctionType>(def.declaration->bound_type);
        return result;
    }

    ASTNode a { arguments };
    ASTNode f { function };

    auto         arg_types = try_bind(arguments);
    auto const  &type_descr = get<TypeList>(arg_types);
    auto        &args { get<ArgumentList>(arguments).arguments };
    std::wstring name;
    Strings      names;
    ASTNodes     type_args { };
    if (auto const *id = get_if<Identifier>(callable); id != nullptr) {
        name = id->identifier;
        names = { name };
    } else if (auto const &stamped_id = get_if<StampedIdentifier>(callable); stamped_id != nullptr) {
        name = stamped_id->identifier;
        type_args = stamped_id->arguments;
        names = { name };
    } else if (is<IdentifierList>(callable)) {
        auto const &callable_list { get<IdentifierList>(callable) };
        names = callable_list.identifiers;
        name = join(callable_list.identifiers, L"."sv);
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
            auto const &func_params_descr = get<TypeList>(func_type_descr.parameters);
            if (type_descr.types.size() != func_params_descr.types.size()) {
                return nullptr;
            }
            ASTNodes reference_nodes;
            auto     made_reference_node { false };
            for (auto [ix, arg_param] : std::views::zip(args, type_descr.types, func_params_descr.types) | std::ranges::views::enumerate) {
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
                a = normalize(make_node<ArgumentList>(arguments, reference_nodes));
                try_bind(a);
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
            try_bind(func_def);
            bound_overloads.push_back(func_def);
        }
    }
    if (type_args.empty()) {
        for (auto const &func_def : bound_overloads) {
            if (auto const ret = match_non_generic_function(func_def); !ret.has_value()) {
                return BindError { ret.error() };
            } else if (ret.value() != nullptr) {
                f = ret.value();
                break;
            }
        }
        if (f == nullptr) {
            for (auto const &func_def : bound_overloads) {
                if (auto const ret = match_generic_function(func_def); !ret.has_value()) {
                    return BindError { ret.error() };
                } else if (ret.value() != nullptr) {
                    f = ret.value();
                    break;
                }
            }
        }
    } else {
        for (auto const &func_def : bound_overloads) {
            if (auto const ret = match_generic_function(func_def); !ret.has_value()) {
                return BindError { ret.error() };
            } else if (ret.value() != nullptr) {
                f = ret.value();
                break;
            }
        }
    }
    if (f != nullptr) {
        auto const &func { get<FunctionDefinition>(f) };
        auto const &func_type_descr { get<FunctionType>(func.declaration->bound_type) };
        auto const &param_types { get<TypeList>(func_type_descr.parameters).types };
        for (auto const &[arg, param_type] : std::ranges::views::zip(args, param_types)) {
            // WTF
        }
        if (a != arguments || f != function) {
            make_node<Call>(n, callable, a, f);
        }
        return func_type_descr.result;
    }
    if (parser.pass == 0) {
        return BindError { ASTStatus::Undetermined };
    }
    return n.bind_error(L"Unresolved function `{}{}`", name, arg_types->to_string());
}

ExternLink::ExternLink(std::wstring link_name)
    : link_name(std::move(link_name))
{
}

BindResult ExternLink::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

FunctionDeclaration::FunctionDeclaration(std::wstring name, ASTNodes generics, ASTNodes parameters, ASTNode return_type)
    : name(std::move(name))
    , generics(std::move(generics))
    , parameters(std::move(parameters))
    , return_type(std::move(return_type))
{
}

ASTNode FunctionDeclaration::normalized(ASTNode const &n) const
{
    return make_node<FunctionDeclaration>(
        n,
        std::wstring { name },
        normalize(generics),
        normalize(parameters),
        normalize(return_type));
}

BindResult FunctionDeclaration::bind(ASTNode const &) const
{
    return TypeRegistry::the().function_of(
        try_bind_nodes(parameters),
        get<TypeType>(try_bind(return_type)).type);
}

FunctionDefinition::FunctionDefinition(std::wstring name, ASTNode declaration, ASTNode implementation)
    : name(std::move(name))
    , declaration(std::move(declaration))
    , implementation(std::move(implementation))
{
    assert(this->declaration != nullptr);
}

FunctionDefinition::FunctionDefinition(std::wstring name)
    : name(std::move(name))
{
}

std::wstring FunctionDefinition::mangled_name(ASTNode const &n) const
{
    switch (visibility) {
    case Visibility::Static:
    case Visibility::Public:
        assert(n->bound_type);
        return std::format(L"{}${}", name, n->bound_type->encode());
    case Visibility::Export:
        return name;
    default:
        UNREACHABLE();
    }
}

ASTNode FunctionDefinition::normalized(ASTNode const &n) const
{
    n->init_namespace();
    auto ret = make_node<FunctionDefinition>(n, std::wstring { name }, normalize(declaration), normalize(implementation));
    return ret;
}

BindResult FunctionDefinition::bind(ASTNode const &n) const
{
    assert(n != nullptr);
    auto &parser { *(n.repo) };
    bool  register_me = declaration->bound_type == nullptr;
    if (auto maybe { bind_declaration(declaration, n) }; !maybe.has_value()) {
        return BindError { maybe.error() };
    }
    auto t = declaration->bound_type;
    assert(is<FunctionType>(t));
    if (register_me) {
        if (auto const &found = n->ns->parent->find_function(name, t); found != nullptr && found != n) {
            return n.bind_error(L"Duplicate overload for function `{}` with type `{}`", name, t);
        }
        n->ns->parent->register_function(name, n);
    }
    auto &decl = get<FunctionDeclaration>(declaration);
    if (!decl.generics.empty()) {
        return TypeRegistry::void_;
    }
    for (auto const &param : decl.parameters) {
        auto parameter = get<Parameter>(param);
        if (!parser.has_variable({ parameter.name })) {
            parser.register_variable(parameter.name, param);
        }
    }
    try_bind(implementation);
    return t;
}

Parameter::Parameter(std::wstring name, ASTNode type_name)
    : name(std::move(name))
    , type_name(std::move(type_name))
{
}

ASTNode Parameter::normalized(ASTNode const &n) const
{
    return make_node<Parameter>(n, std::wstring { name }, normalize(type_name));
}

BindResult Parameter::bind(ASTNode const &) const
{
    return get<TypeType>(try_bind(type_name)).type;
}

}
