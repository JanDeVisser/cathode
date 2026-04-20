/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ostream>
#include <string>
#include <string_view>
#include <unistd.h>

#include <Util/Defer.h>
#include <Util/IO.h>
#include <Util/Lexer.h>
#include <Util/Logging.h>
#include <Util/StringUtil.h>
#include <Util/TokenLocation.h>
#include <Util/Utf8.h>

#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

namespace Lang {

using namespace Util;

char const *SyntaxNodeType_name(SyntaxNodeType type)
{
    switch (type) {
#undef S
#define S(T)                \
    case SyntaxNodeType::T: \
        return #T;
        SyntaxNodeTypes(S)
#undef S
            default : UNREACHABLE();
    }
}

size_t ASTNode::value() const
{
    return repo->hunt(*this);
}

TokenLocation ASTNode::operator+(ASTNode const &other)
{
    assert(*this && other);
    assert(repo == other.repo);
    return (*this)->location + other->location;
}

TokenLocation ASTNode::operator+(TokenLocation const &other)
{
    assert(*this);
    return (*this)->location + other;
}

void ASTNode::error(std::wstring const &msg) const
{
    repo->append((*this)->location, msg);
}

void ASTNode::error(std::string const &msg) const
{
    repo->append((*this)->location, msg);
}

BindError ASTNode::bind_error(std::wstring const &msg) const
{
    return repo->bind_error((*this)->location, msg);
}

BindError ASTNode::bind_error(LangError error) const
{
    return repo->bind_error(std::move(error));
}

void ASTNodeImpl::init_namespace()
{
    Parser &parser = *(id.repo);
    NSNode  parent { nullptr };
    if (!parser.namespaces.empty()) {
        parent = parser.namespaces.back();
    }
    ns = NSNode { &parser.namespace_nodes, id, parent };
    parser.push_namespace(id);
}

template<typename N>
bool is_constant(ASTNode const &, N const &)
{
    return false;
}

template<Constant C>
bool is_constant(ASTNode const &, C const &)
{
    return true;
}

template<>
bool is_constant(ASTNode const &, ExpressionList const &impl)
{
    return std::ranges::all_of(
        impl.expressions,
        [](ASTNode const &expr) -> bool {
            return is_constant(expr);
        });
}

template<>
bool is_constant(ASTNode const &, TagValue const &impl)
{
    return impl.operand == nullptr;
}

template<>
bool is_constant(ASTNode const &, VariableDeclaration const &impl)
{
    return impl.is_const && impl.initializer != nullptr && is_constant(impl.initializer);
}

bool is_constant(ASTNode const &n)
{
    return std::visit(
        [&n](auto const &impl) -> bool {
            return is_constant(n, impl);
        },
        n->node);
}

ASTNode normalize(ASTNode node)
{
    if (node == nullptr) {
        return nullptr;
    }
    auto &parser { *(node.repo) };
    trace(L"[->N] {:t}", node);
    ASTNode ret = node;
    if (node->status < ASTStatus::Normalized) {
        size_t stack_size { parser.namespaces.size() };
        if (node->ns != nullptr) {
            node->id.repo->push_namespace(node);
        }
        ret = std::visit(
            [&node](auto const impl) {
                return impl.normalized(node);
            },
            node->node);
        while (parser.namespaces.size() > stack_size) {
            ret.repo->pop_namespace(node);
        }
        if (ret != nullptr && ret->status < ASTStatus::Normalized) {
            ret->status = ASTStatus::Normalized;
        }
    }
    trace(L"[N->] {:t}", ret);
    return ret;
}

ASTNodes normalize(ASTNodes nodes)
{
    ASTNodes normalized { };
    std::ranges::for_each(
        nodes,
        [&normalized](auto const &n) {
            auto ret = normalize(n);
            if (ret != nullptr) {
                normalized.push_back(ret);
            }
        });
    return normalized;
}

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
            [&node](auto const impl) {
                auto id { node->id.id.value() };
                return impl.bind(node);
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

std::wostream &operator<<(std::wostream &os, Lang::ASTNode const &node)
{
    os << SyntaxNodeType_name(node->type()) << " (" << node->location.index << ".." << node->location.index + node->location.length << ") ";
    return os;
}
