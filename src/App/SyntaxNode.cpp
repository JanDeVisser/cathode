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

bool is_constant(ASTNode const &n)
{
    return std::visit([](auto const &impl) -> bool {
        return is_constant<decltype(impl)>();
    },
        n->node);
}

Block::Block(ASTNodes statements)
    : statements(std::move(statements))
{
}

DeferStatement::DeferStatement(ASTNode statement)
    : statement(std::move(statement))
{
}

Comptime::Comptime(std::wstring_view script_text, ASTNode const &block)
    : script_text(script_text)
    , statements(std::move(block))
{
}

BoolConstant::BoolConstant(bool value)
    : value(value)
{
}
Decimal::Decimal(std::wstring_view whole, std::wstring_view fraction, std::wstring_view exponent)
{
    std::string s = as_utf8(whole);
    if (!fraction.empty()) {
        s += ".";
        s += as_utf8(fraction);
    }
    if (!exponent.empty()) {
        s += "E";
        s += as_utf8(exponent);
    }
    auto dbl { string_to_double(s) };
    assert(dbl);
    value = *dbl;
}

Nullptr::Nullptr()
{
}

Number::Number(std::wstring_view number, Radix radix)
{
    if (auto v = string_to_integer<int64_t>(number, static_cast<int>(radix)); v) {
        value = *v;
    } else {
        fatal(L"Cannot parse `{}` as an integer with radix `{}`", number, static_cast<int>(radix));
    }
}

Number::Number(Int value)
    : value(value)
{
}

QuotedString::QuotedString(std::wstring_view str, QuoteType type)
    : string(str)
    , quote_type(type)
{
}

Embed::Embed(std::wstring_view file_name)
    : file_name(file_name)
{
}

EnumValue::EnumValue(std::wstring label, ASTNode value, ASTNode payload)
    : label(std::move(label))
    , value(std::move(value))
    , payload(std::move(payload))
{
}

Enum::Enum(std::wstring name, ASTNode underlying_type, ASTNodes values)
    : name(std::move(name))
    , underlying_type(std::move(underlying_type))
    , values(std::move(values))
{
}

BinaryExpression::BinaryExpression(ASTNode lhs, Operator const op, ASTNode rhs)
    : lhs(std::move(lhs))
    , op(op)
    , rhs(std::move(rhs))
{
}

UnaryExpression::UnaryExpression(Operator const op, ASTNode operand)
    : op(op)
    , operand(std::move(operand))
{
}

ExpressionList::ExpressionList(ASTNodes expressions)
    : expressions(std::move(expressions))
{
}

ExternLink::ExternLink(std::wstring link_name)
    : link_name(std::move(link_name))
{
}

FunctionDeclaration::FunctionDeclaration(std::wstring name, ASTNodes generics, ASTNodes parameters, ASTNode return_type)
    : name(std::move(name))
    , generics(std::move(generics))
    , parameters(std::move(parameters))
    , return_type(std::move(return_type))
{
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

Parameter::Parameter(std::wstring name, ASTNode type_name)
    : name(std::move(name))
    , type_name(std::move(type_name))
{
}

Call::Call(ASTNode callable, ASTNode args)
    : callable(std::move(callable))
    , arguments(std::move(args))
{
    if (!is<Identifier>(this->callable) && !is<StampedIdentifier>(this->callable) && !is<ExpressionList>(this->callable)) {
        NYI("Callable must be a function name, not a {}", SyntaxNodeType_name(this->callable->type()));
    }
    assert(this->arguments != nullptr);
}

IfStatement::IfStatement(ASTNode condition, ASTNode if_branch, ASTNode else_branch)
    : condition(condition)
    , if_branch(if_branch)
    , else_branch(else_branch)
{
    assert(condition != nullptr && if_branch != nullptr);
}

Import::Import(std::wstring file_name)
    : file_name(std::move(file_name))
{
}

Include::Include(std::wstring_view file_name)
    : file_name(file_name)
{
}

ForStatement::ForStatement(std::wstring var, ASTNode expr, ASTNode statement)
    : range_variable(std::move(var))
    , range_expr(std::move(expr))
    , statement(statement)
{
    assert(this->range_expr != nullptr);
    assert(this->statement != nullptr);
}

LoopStatement::LoopStatement(Label label, ASTNode statement)
    : label(std::move(label))
    , statement(std::move(statement))
{
    assert(this->statement != nullptr);
}

WhileStatement::WhileStatement(Label label, ASTNode condition, ASTNode statement)
    : label(std::move(label))
    , condition(std::move(condition))
    , statement(std::move(statement))
{
    assert(this->condition != nullptr && this->statement != nullptr);
}

Module::Module(std::wstring name, std::wstring source)
    : name(std::move(name))
    , source(std::move(source))
{
}

Module::Module(std::wstring name, std::wstring source, ASTNodes const &statements)
    : name(std::move(name))
    , source(std::move(source))
    , statements(statements)
{
}

Program::Program(std::wstring name, std::wstring source)
    : name(std::move(name))
    , source(std::move(source))
{
}

Program::Program(std::wstring name, ASTNodes statements)
    : name(std::move(name))
    , statements(std::move(statements))
{
}

PublicDeclaration::PublicDeclaration(std::wstring name, ASTNode declaration)
    : name(std::move(name))
    , declaration(std::move(declaration))
{
    assert(this->declaration != nullptr);
}

Break::Break(Label label)
    : label(std::move(label))
{
}

Continue::Continue(Label label)
    : label(std::move(label))
{
}

Return::Return(ASTNode expression)
    : expression(std::move(expression))
{
}

Yield::Yield(Label label, ASTNode statement)
    : label(std::move(label))
    , statement(std::move(statement))
{
}

StructMember::StructMember(std::wstring label, ASTNode type)
    : label(std::move(label))
    , member_type(std::move(type))
{
    assert(this->member_type != nullptr);
}

Struct::Struct(std::wstring name, ASTNodes members)
    : name(std::move(name))
    , members(std::move(members))
{
}

TagValue::TagValue(int64_t tag_value, std::wstring label, pType payload_type, ASTNode payload)
    : tag_value(tag_value)
    , label(std::move(label))
    , payload_type(std::move(payload_type))
    , payload(std::move(payload))
{
}

TagValue::TagValue(ASTNode operand, int64_t tag_value, std::wstring label, pType payload_type, ASTNode payload)
    : operand(std::move(operand))
    , tag_value(tag_value)
    , label(std::move(label))
    , payload_type(std::move(payload_type))
    , payload(std::move(payload))
{
}

TypeSpecification::TypeSpecification(TypeSpecificationDescription description)
    : description(description)
{
}

Void::Void()
{
}

Identifier::Identifier(std::wstring_view const identifier)
    : identifier(identifier)
{
}

StampedIdentifier::StampedIdentifier(std::wstring_view const identifier, ASTNodes arguments)
    : identifier(identifier)
    , arguments(std::move(arguments))
{
}

VariableDeclaration::VariableDeclaration(std::wstring name, ASTNode type_name, ASTNode initializer, bool is_const)
    : name(std::move(name))
    , type_name(type_name)
    , initializer(std::move(initializer))
    , is_const(is_const)
{
}
}

std::wostream &operator<<(std::wostream &os, Lia::ASTNode const &node)
{
    os << SyntaxNodeType_name(node->type()) << " (" << node->location.index << ".." << node->location.index + node->location.length << ") ";
    return os;
}
