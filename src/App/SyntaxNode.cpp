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

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

namespace Lia {

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

BindError ASTNode::bind_error(LiaError error) const
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

}

std::wostream &operator<<(std::wostream &os, Lia::ASTNode const &node)
{
    os << SyntaxNodeType_name(node->type()) << " (" << node->location.index << ".." << node->location.index + node->location.length << ") ";
    return os;
}
