/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ranges>

#include <Util/Logging.h>
#include <Util/Utf8.h>

#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

namespace Lang {

// #define DEBUG_NAMESPACE_STACK

Namespace::Namespace(ASTNode node, NSNode parent)
    : node(std::move(node))
    , parent(std::move(parent))
{
}

bool Namespace::contains(std::wstring const &name) const
{
    if (node && is<ModuleProxy>(node)) {
        auto proxy { get<ModuleProxy>(node) };
        if (proxy.module && proxy.module->ns->contains(name)) {
            return true;
        }
    }
    if (entries.contains(name)) {
        return true;
    }
    if (parent != nullptr) {
        return parent->contains(name);
    }
    return false;
}

int Namespace::count(std::wstring const &name) const
{
    auto ret = entries.count(name);
    if (node && is<ModuleProxy>(node)) {
        auto proxy { get<ModuleProxy>(node) };
        if (proxy.module) {
            ret += proxy.module->ns->count(name);
        }
    }
    if (parent != nullptr) {
        ret += parent->count(name);
    }
    return ret;
}

std::optional<Namespace::NSEntry> Namespace::at(std::wstring const &name) const
{
    auto a { all(name) };
    if (a.size() == 1) {
        return a[0];
    }
    return { };
}

Namespace::NSEntries Namespace::all(std::wstring const &name) const
{
    std::vector<Namespace::NSEntry> ret { };
    if (node && is<ModuleProxy>(node)) {
        auto proxy { get<ModuleProxy>(node) };
        if (proxy.module) {
            ret.insert_range(ret.end(), proxy.module->ns->all(name));
        }
    }
    if (auto it { entries.find(name) }; it != entries.end()) {
        for (; std::get<std::wstring const>(*it) == name; ++it) {
            ret.emplace_back(std::get<Namespace::NSEntry>(*it));
        }
    }
    if (parent != nullptr) {
        ret.insert_range(ret.end(), parent->all(name));
    }
    return ret;
}

ASTNode Namespace::find_module(std::wstring const &name) const
{
    if (auto e { at(name) }; e && e->index() == Namespace::Module) {
        return std::get<Namespace::Module>(*e);
    }
    return nullptr;
}

void Namespace::register_module(std::wstring const &name, ASTNode module)
{
    assert(!contains(name));
    module->bound_type = TypeRegistry::module;
#ifdef DEBUG_NAMESPACE_STACK
    info(L"[S->m {}] {}", id.id.value(), name);
#endif
    entries.emplace(name, Namespace::NSEntry { std::in_place_index<Namespace::Module>, module });
}

bool Namespace::has_module(std::wstring const &name) const
{
    return find_module(name) != nullptr;
}

pType Namespace::find_type(std::wstring const &name) const
{
    if (auto entry { at(name) }; entry) {
        return std::visit(
            overloads {
                [](pType const &type) -> pType {
                    return type;
                },
                [](auto const &) -> pType {
                    return nullptr;
                } },
            *entry);
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
    assert(!contains(name));
#ifdef DEBUG_NAMESPACE_STACK
    info(L"[S->t {}] {}", id.id.value(), name);
#endif
    entries.emplace(name, type);
}

bool Namespace::has_type(std::wstring const &name) const
{
    return find_type(name) != nullptr;
}

ASTNode Namespace::find_variable(std::wstring const &name) const
{
    if (auto entry { at(name) }; entry && entry->index() == Namespace::Variable) {
        return std::get<Namespace::Variable>(*entry);
    }
    return nullptr;
}

bool Namespace::has_variable(std::wstring const &name) const
{
    return find_variable(name) != nullptr;
}

pType Namespace::type_of(std::wstring const &name) const
{
    if (auto entry { at(name) }; entry) {
        switch (entry->index()) {
        case Namespace::Variable:
            return std::get<Namespace::Variable>(*entry)->bound_type;
        case Namespace::Function:
            return nullptr;
        case Namespace::Module:
            return TypeRegistry::module;
        case Namespace::Type:
            return TypeRegistry::the().type_of(std::get<pType>(*entry));
        default:
            UNREACHABLE();
        }
    }
    return nullptr;
}

void Namespace::register_variable(std::wstring name, ASTNode variable)
{
    assert(!contains(name));
#ifdef DEBUG_NAMESPACE_STACK
    info(L"[S->v {}] {}", id.id.value(), name);
#endif
    entries.emplace(name, Namespace::NSEntry { std::in_place_index<Namespace::Variable>, variable });
}

bool Namespace::has_function(std::wstring const &name) const
{
    return std::ranges::any_of(all(name),
        [](auto const &e) -> bool {
            return e.index() == 1;
        });
}

ASTNodes Namespace::find_functions(std::wstring const &name) const
{
    ASTNodes ret { };
    std::ranges::for_each(
        all(name) | std::ranges::views::enumerate,
        [&ret](auto const &t) {
            auto const &[ix, e] = t;
            if (e.index() == Namespace::Function) {
                assert(ret.size() == ix);
                ret.emplace_back(std::get<Namespace::Function>(e));
            } else {
                assert(ix == 0);
            }
        });
    return ret;
}

ASTNode Namespace::find_function(std::wstring const &name, pType const &type) const
{
    auto functions { find_functions(name) };
    auto it = std::ranges::find_if(
        functions,
        [&type](auto const &func) -> bool {
            FunctionDefinition const &def = get<FunctionDefinition>(func);
            if (def.declaration->bound_type == type) {
                return true;
            }
            return false;
        });
    if (it == functions.end()) {
        return nullptr;
    }
    auto ret { *it };
    ++it;
    assert(it == functions.end());
    return ret;
}

ASTNodes Namespace::find_overloads(std::wstring const &name, ASTNodes const &type_args) const
{
    auto     functions { find_functions(name) };
    ASTNodes ret { };
    std::ranges::for_each(
        find_functions(name),
        [&ret, &type_args](auto const &node) {
            if (get<FunctionDeclaration>(get<FunctionDefinition>(node).declaration).generics.size() >= type_args.size()) {
                ret.emplace_back(node);
            }
        });
    return ret;
}

void Namespace::register_function(std::wstring name, ASTNode fnc)
{
    assert(!contains(name) || has_function(name));
#ifdef DEBUG_NAMESPACE_STACK
    info(L"[S->f {}] {}", id.id.value(), name);
#endif
    entries.emplace(name, Namespace::NSEntry { std::in_place_index<Namespace::Function>, fnc });
}
}
