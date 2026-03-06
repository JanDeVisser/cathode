/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <functional>

#include <App/Parser.h>
#include <App/SyntaxNode.h>

namespace Lia {

Namespace::Namespace(ASTNode node, NSNode parent)
    : node(std::move(node))
    , parent(std::move(parent))
{
}

bool Namespace::is_registered(std::wstring const &name) const
{
    if (has_type(name)) {
        return true;
    }
    if (has_variable(name)) {
        return true;
    }
    return has_function(name);
}

pType Namespace::find_type(std::wstring const &name) const
{
    if (has_type(name)) {
        return types.at(name);
    }
    if (auto p = parent; p != nullptr) {
        return p->find_type(name);
    }
    return nullptr;
}

ASTNode Namespace::current_function() const
{
    if (parent == nullptr) {
        return nullptr;
    }
    auto p = parent->node;
    if (is<FunctionDefinition>(p)) {
        return p;
    }
    return parent->current_function();
}

void Namespace::register_type(std::wstring name, pType type)
{
    assert(!types.contains(name));
    types[name] = std::move(type);
}

bool Namespace::has_type(std::wstring const &name) const
{
    return types.contains(name);
}

ASTNode Namespace::find_variable(std::wstring const &name) const
{
    if (variables.contains(name)) {
        return variables.at(name);
    }
    if (auto p = parent; p != nullptr) {
        return p->find_variable(name);
    }
    return nullptr;
}

bool Namespace::has_variable(std::wstring const &name) const
{
    return variables.contains(name);
}

pType Namespace::type_of(std::wstring const &name) const
{
    auto n = find_variable(name);
    if (n != nullptr) {
        return n->bound_type;
    }
    auto t = find_type(name);
    if (t != nullptr) {
        return TypeRegistry::the().type_of(t);
    }
    return nullptr;
}

void Namespace::register_variable(std::wstring name, ASTNode node)
{
    assert(!variables.contains(name));
    variables.emplace(name, std::move(node));
}

bool Namespace::has_function(std::wstring const &name) const
{
    return functions.contains(name) && !functions.at(name).empty();
}

ASTNode Namespace::find_function_here(std::wstring name, pType const &type) const
{
    assert(is<FunctionType>(type));
    if (!functions.contains(name)) {
        return nullptr;
    }
    auto overloads = functions.at(name);
    for (auto const &function : overloads) {
        auto const &def = get<FunctionDefinition>(function);
        if (def.declaration->bound_type == type) {
            return function;
        }
    }
    return nullptr;
}

ASTNode Namespace::find_function(std::wstring const &name, pType const &type) const
{
    assert(is<FunctionType>(type));
    if (auto here = find_function_here(name, type); here != nullptr) {
        return here;
    }
    if (auto p = parent; p != nullptr) {
        return p->find_function(name, type);
    }
    return nullptr;
}

ASTNode Namespace::find_function_by_arg_list(std::wstring const &name, pType const &type) const
{
    assert(is<TypeList>(type));
    auto const &type_descr = std::get<TypeList>(type->description);
    if (functions.contains(name)) {
        auto const &overloads = functions.at(name);
        for (auto const &overload : overloads) {
            auto const &def = get<FunctionDefinition>(overload);
            auto const &func_type = def.declaration->bound_type;
            if (!is<FunctionType>(func_type)) {
                continue;
            }
            auto const &func_type_descr = std::get<FunctionType>(func_type->description);
            if (func_type_descr.parameters == type_descr.types) {
                return overload;
            }
        }
    }
    if (auto p = parent; p != nullptr) {
        return p->find_function_by_arg_list(name, type);
    }
    return nullptr;
}

ASTNodes Namespace::find_overloads(std::wstring const &name, ASTNodes const &type_args) const
{
    std::function<void(Namespace const &, ASTNodes &)> find_them;
    find_them = [&name, &find_them, &type_args](Namespace const &ns, ASTNodes &overloads) -> void {
        if (ns.functions.contains(name)) {
            auto const &overloads_of = ns.functions.at(name);
            for (auto const &overload : overloads_of) {
                if (get<FunctionDeclaration>(get<FunctionDefinition>(overload).declaration).generics.size() >= type_args.size()) {
                    overloads.push_back(overload);
                }
            }
        }
        if (auto p = ns.parent; p != nullptr) {
            find_them(*p, overloads);
        }
    };
    ASTNodes ret;
    find_them(*this, ret);
    return ret;
}

void Namespace::register_function(std::wstring name, ASTNode fnc)
{
    auto const &def = get<FunctionDefinition>(fnc);
    assert(fnc->bound_type == nullptr || find_function_here(def.name, fnc->bound_type) == nullptr);
    functions[name].push_back(fnc);
}

void Namespace::unregister_function(std::wstring name, ASTNode const &fnc)
{
    assert(is<FunctionType>(fnc->bound_type));
    if (functions.contains(name)) {
        auto &overloads { functions.at(name) };
        for (auto it = overloads.begin(); it != overloads.end(); ++it) {
            auto &f = *it;
            if (f->bound_type == fnc->bound_type) {
                overloads.erase(it);
                return;
            }
        }
    }
}

}
