/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <iostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

#include <Util/Error.h>
#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

namespace Lang {

using namespace std::literals;

void print_indent(std::ostream &os, int indent)
{
    static std::string spaces;
    while (spaces.length() < 1024) {
        spaces += "    "sv;
    }
    os << spaces.substr(0, indent);
}

template<class N>
void dump(ASTNode const &, N const &, std::ostream &, int)
{
}

template<>
void dump(ASTNode const &, ArgumentList const &impl, std::ostream &os, int indent)
{
    print_indent(os, indent);
    os << "#expressions: " << impl.arguments.size() << "\n";
    for (auto const &expr : impl.arguments) {
        dump(expr, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, BinaryExpression const &impl, std::ostream &os, int const indent)
{
    dump(impl.lhs, os, indent + 4);
    dump(impl.rhs, os, indent + 4);
}

template<class Statements>
    requires std::is_same_v<Statements, Block>
    || std::is_same_v<Statements, Module>
void dump(ASTNode const &, Statements const &impl, std::ostream &os, int indent)
{
    print_indent(os, indent);
    os << "#statements: " << impl.statements.size() << "\n";
    for (auto const &stmt : impl.statements) {
        dump(stmt, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, Call const &impl, std::ostream &os, int indent)
{
    dump(impl.callable, os, indent + 4);
    dump(impl.arguments, os, indent + 4);
    if (impl.function != nullptr) {
        print_indent(os, indent + 4);
        os << to_string(impl.function) << '\n';
    }
}

template<>
void dump(ASTNode const &, Comptime const &impl, std::ostream &os, int indent)
{
    if (impl.statements) {
        dump(impl.statements, os, indent + 4);
    } else if (!impl.output.empty()) {
        print_indent(os, indent + 4);
        os << as_utf8(impl.output) << '\n';
    } else {
        print_indent(os, indent + 4);
        os << as_utf8(impl.script_text) << '\n';
    }
}

template<class StatementWrapper>
    requires std::is_same_v<StatementWrapper, DeferStatement>
    || std::is_same_v<StatementWrapper, LoopStatement>
    || std::is_same_v<StatementWrapper, Yield>
void dump(ASTNode const &, StatementWrapper const &impl, std::ostream &os, int indent)
{
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &, Enum const &impl, std::ostream &os, int indent)
{
    for (auto const &v : impl.values) {
        dump(v, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, Return const &impl, std::ostream &os, int indent)
{
    dump(impl.expression, os, indent + 4);
}

template<>
void dump(ASTNode const &, ExpressionList const &impl, std::ostream &os, int indent)
{
    print_indent(os, indent);
    os << "#expressions: " << impl.expressions.size() << "\n";
    for (auto const &expr : impl.expressions) {
        dump(expr, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, ForStatement const &impl, std::ostream &os, int indent)
{
    dump(impl.range_expr, os, indent + 4);
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &, FunctionDeclaration const &impl, std::ostream &os, int indent)
{
    for (auto const &gen : impl.generics) {
        dump(gen, os, indent + 4);
    }
    for (auto const &param : impl.parameters) {
        dump(param, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, FunctionDefinition const &impl, std::ostream &os, int indent)
{
    dump(impl.declaration, os, indent + 4);
    dump(impl.implementation, os, indent + 4);
}

template<>
void dump(ASTNode const &, IfStatement const &impl, std::ostream &os, int indent)
{
    dump(impl.condition, os, indent + 4);
    dump(impl.if_branch, os, indent + 4);
    dump(impl.else_branch, os, indent + 4);
}

template<>
void dump(ASTNode const &, LoopStatement const &impl, std::ostream &os, int indent)
{
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &n, Program const &impl, std::ostream &os, int indent)
{
    if (get_option("dump-std-lib")) {
        for (auto const &stmt : impl.statements) {
            dump(stmt, os, indent + 4);
        }
    }
    auto &parser { *(n.repo) };
    for (auto &[_, mod] : parser.modules) {
        dump(mod, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, PublicDeclaration const &impl, std::ostream &os, int indent)
{
    dump(impl.declaration, os, indent + 4);
}

template<>
void dump(ASTNode const &, Struct const &impl, std::ostream &os, int indent)
{
    for (auto const &m : impl.members) {
        dump(m, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, SwitchCase const &impl, std::ostream &os, int indent)
{
    dump(impl.case_value, os, indent + 4);
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &, SwitchStatement const &impl, std::ostream &os, int indent)
{
    dump(impl.switch_value, os, indent + 4);
    for (auto const &c : impl.switch_cases) {
        dump(c, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &, UnaryExpression const &impl, std::ostream &os, int indent)
{
    dump(impl.operand, os, indent + 4);
}

template<>
void dump(ASTNode const &, VariableDeclaration const &impl, std::ostream &os, int indent)
{
    dump(impl.initializer, os, indent + 4);
}

template<>
void dump(ASTNode const &, WhileStatement const &impl, std::ostream &os, int indent)
{
    dump(impl.condition, os, indent + 4);
    dump(impl.statement, os, indent + 4);
}

/* ======================================================================== */

template<class N>
std::string to_string(ASTNode const &, N const &)
{
    return "";
}

template<class Expr>
    requires std::is_same_v<Expr, BinaryExpression>
    || std::is_same_v<Expr, UnaryExpression>
std::string to_string(ASTNode const &, Expr const &impl)
{
    return Operator_name(impl.op);
}

template<>
std::string to_string(ASTNode const &, Alias const &impl)
{
    return std::format("{} = {}", as_utf8(impl.name), to_string(impl.aliased_type));
}

template<>
std::string to_string(ASTNode const &, BoolConstant const &impl)
{
    return (impl.value) ? "True" : "False";
}

template<>
std::string to_string(ASTNode const &, Call const &impl)
{
    return to_string(impl.callable);
}

template<>
std::string to_string(ASTNode const &, Decimal const &impl)
{
    std::stringstream os;
    os << impl.value;
    return std::string { os.view() };
}

template<>
std::string to_string(ASTNode const &, Number const &impl)
{
    std::stringstream os;
    std::visit(
        [&os](auto v) {
            os << v;
        },
        impl.value);
    os << " <" << impl.value.index() << ">";
    return os.str();
}

template<class N>
    requires std::is_same_v<N, Embed>
    || std::is_same_v<N, Include>
std::string to_string(ASTNode const &, N const &impl)
{
    return as_utf8(impl.file_name);
}

template<>
std::string to_string(ASTNode const &, Import const &impl)
{
    return as_utf8(join(impl.file_name, L"."sv));
}

template<>
std::string to_string(ASTNode const &, Enum const &impl)
{
    std::stringstream os;
    os << as_utf8(impl.name);
    if (impl.underlying_type != nullptr) {
        os << ": " << to_string(impl.underlying_type);
    }
    return os.str();
}

template<>
std::string to_string(ASTNode const &, EnumValue const &impl)
{
    std::string ret = as_utf8(impl.label);
    if (impl.value != nullptr) {
        ret += " = "sv;
        ret += to_string(impl.value);
    }
    if (impl.payload != nullptr) {
        ret += " ("sv;
        ret += to_string(impl.payload);
        ret += ")"sv;
    }
    return ret;
}

template<>
std::string to_string(ASTNode const &, Extern const &impl)
{
    return std::format("{} function in library `{}`", impl.declarations.size(), as_utf8(impl.library));
}

template<>
std::string to_string(ASTNode const &, ExternLink const &impl)
{
    return as_utf8(impl.link_name);
}

template<>
std::string to_string(ASTNode const &, ForStatement const &impl)
{
    return as_utf8(impl.range_variable);
}

template<>
std::string to_string(ASTNode const &, FunctionDeclaration const &impl)
{
    std::stringstream os;
    os << as_utf8(impl.name);
    if (!impl.generics.empty()) {
        char sep { '<' };
        for (auto const &gen : impl.generics) {
            os << sep << as_utf8(get<Identifier>(gen).identifier);
            sep = ',';
        }
        os << '>';
    }
    os << ": " << to_string(impl.return_type);
    return os.str();
}

template<>
std::string to_string(ASTNode const &, FunctionDefinition const &impl)
{
    return as_utf8(impl.name);
}

template<class N>
    requires std::is_same_v<N, Break>
    || std::is_same_v<N, Continue>
    || std::is_same_v<N, LoopStatement>
    || std::is_same_v<N, SwitchStatement>
    || std::is_same_v<N, WhileStatement>
    || std::is_same_v<N, Yield>
std::string to_string(ASTNode const &, N const &impl)
{
    if (impl.label) {
        return as_utf8(*impl.label);
    }
    return "";
}

template<class N>
    requires std::is_same_v<N, Identifier> || std::is_same_v<N, StampedIdentifier>
std::string to_string(ASTNode const &, N const &impl)
{
    return as_utf8(impl.identifier);
}

template<>
std::string to_string(ASTNode const &, IdentifierList const &impl)
{
    return as_utf8(join(impl.identifiers, L"."sv));
}

template<>
std::string to_string(ASTNode const &, Module const &impl)
{
    return as_utf8(impl.name);
}

template<>
std::string to_string(ASTNode const &, Parameter const &impl)
{
    return std::format("{}: {}", as_utf8(impl.name), to_string(impl.type_name));
}

template<>
std::string to_string(ASTNode const &, QuotedString const &impl)
{
    return as_utf8(impl.string);
}

template<>
std::string to_string(ASTNode const &, CString const &impl)
{
    return impl.string;
}

template<>
std::string to_string(ASTNode const &, String const &impl)
{
    return as_utf8(impl.string);
}

template<>
std::string to_string(ASTNode const &, Struct const &impl)
{
    return as_utf8(impl.name);
}

template<>
std::string to_string(ASTNode const &, StructMember const &impl)
{
    return std::format("{} {}", as_utf8(impl.label), to_string(impl.member_type));
}

template<TagReference TagRef>
std::string to_string(ASTNode const &n, TagRef const &impl)
{
    std::stringstream ss {};
    ss << impl.tag_value;
    if (n->bound_type) {
        std::visit(
            overloads {
                [&ss, &impl](enumerated_type auto const &enoom) -> void {
                    ss << "=" << as_utf8(enoom.label_for(impl.tag_value).value_or(L"??"));
                },
                [](auto const &) -> void {
                    std::unreachable();
                } },
            n->bound_type->description);
    }
    return ss.str();
}

template<>
std::string to_string(ASTNode const &n, TagPayload const &impl)
{
    std::stringstream ss {};
    ss << impl.tag_value;
    if (n->bound_type) {
        std::visit(
            overloads {
                [&ss, &impl](enumerated_type auto const &enoom) -> void {
                    ss << "=" << as_utf8(enoom.label_for(impl.tag_value).value_or(L"??"));
                },
                [](auto const &) -> void {
                    std::unreachable();
                } },
            n->bound_type->description);
    }
    return ss.str();
}

template<>
std::string to_string(ASTNode const &, TypeSpecification const &impl)
{
    return std::visit(
        overloads {
            [](TypeNameNode const &d) -> std::string {
                return std::format("<{}>", as_utf8(join(d.name, std::wstring_view { L"," })));
            },
            [](ReferenceDescriptionNode const &d) -> std::string {
                return std::format("&{}", to_string(d.referencing));
            },
            [](SliceDescriptionNode const &d) -> std::string {
                return std::format("[]{}", to_string(d.slice_of));
            },
            [](ZeroTerminatedArrayDescriptionNode const &d) -> std::string {
                return std::format("[0]{}", to_string(d.array_of));
            },
            [](ArrayDescriptionNode const &d) -> std::string {
                return std::format("[{}]{}", d.size, to_string(d.array_of));
            },
            [](DynArrayDescriptionNode const &d) -> std::string {
                return std::format("[*]{}", to_string(d.array_of));
            },
            [](OptionalDescriptionNode const &d) -> std::string {
                return std::format("{}?", to_string(d.optional_of));
            },
            [](PointerDescriptionNode const &d) -> std::string {
                return std::format("{}*", to_string(d.referencing));
            },
            [](ResultDescriptionNode const &d) -> std::string {
                return std::format("{}/{}", to_string(d.success), to_string(d.error));
            },
        },
        impl.description);
}

template<>
std::string to_string(ASTNode const &, VariableDeclaration const &impl)
{
    std::stringstream os;
    if (impl.is_const) {
        os << "const ";
    }
    os << as_utf8(impl.name);
    if (impl.type_name != nullptr) {
        os << ": " << to_string(impl.type_name);
    }
    return os.str();
}

/* ======================================================================== */

std::string to_string(ASTNode const &node)
{
    if (node == nullptr) {
        return "[null]";
    }
    std::stringstream os;
    std::string       color {};
    switch (node->status) {
    case ASTStatus::Initialized:
    case ASTStatus::Normalized:
        break;
    case ASTStatus::Bound:
        color = "\x1b[32m";
        break;
    case ASTStatus::Undetermined:
        color = "\x1b[33m";
        break;
    case ASTStatus::Ambiguous:
    case ASTStatus::BindErrors:
    case ASTStatus::InternalError:
        color = "\x1b[31m";
        break;
    }
    os << color << SyntaxNodeType_name(node->type());
    if (!color.empty()) {
        os << "\x1b[m";
    }
    os << ' ';
    std::visit(
        [&node, &os](auto const &impl) {
            os << to_string(node, impl);
        },
        node->node);
    if (node->bound_type != nullptr) {
        os << " -> " << as_utf8(node->bound_type->name);
    }
    os << " ";
    switch (node->status) {
    case ASTStatus::Initialized:
    case ASTStatus::Normalized:
    case ASTStatus::Bound:
    case ASTStatus::Undetermined:
        break;
    case ASTStatus::Ambiguous:
        os << "Ambiguous";
        break;
    case ASTStatus::BindErrors:
        os << "BindErrors";
        break;
    case ASTStatus::InternalError:
        os << "InternalError";
        break;
    }
    return os.str();
}

void header(ASTNode const &node, std::ostream &os)
{
    os << node.id.value()
       << " "
       << " (" << node->location.index
       << ".." << node->location.index + node->location.length
       << ") "
       << to_string(node);
}

void dump(ASTNode const &node, std::ostream &os, int indent)
{
    if (node == nullptr) {
        return;
    }
    print_indent(os, indent);
    header(node, os);
    if (node->ns) {
        os << " || ns "
           << node->ns.id.value();
        if (node->ns->parent) {
            os << "<-" << node->ns->parent.id.value();
        }
        os << ' ' << node->ns->entries.size();
    }
    os << std::endl;
    std::visit(
        [&node, &indent, &os](auto const &impl) {
            dump(node, impl, os, indent);
        },
        node->node);
}

void dump(ASTNodes const &nodes, std::ostream &os, int indent)
{
    for (auto const &node : nodes) {
        dump(node, os, indent);
    }
}

}
