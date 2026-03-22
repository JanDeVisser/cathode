/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>

namespace Lia {

void print_indent(std::wostream &os, int indent)
{
    static std::wstring spaces;
    while (spaces.length() < 1024) {
        spaces += L"    ";
    }
    os << spaces.substr(0, indent);
}

template<class N>
void dump(ASTNode const &n, N const &impl, std::wostream &os, int indent)
{
}

template<>
void dump(ASTNode const &, BinaryExpression const &impl, std::wostream &os, int const indent)
{
    dump(impl.lhs, os, indent + 4);
    dump(impl.rhs, os, indent + 4);
}

template<class Statements>
    requires std::is_same_v<Statements, Block>
    || std::is_same_v<Statements, Module>
void dump(ASTNode const &n, Statements const &impl, std::wostream &os, int indent)
{
    print_indent(os, indent);
    os << "#statements: " << impl.statements.size() << "\n";
    for (auto const &stmt : impl.statements) {
        dump(stmt, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, Call const &impl, std::wostream &os, int indent)
{
    dump(impl.callable, os, indent + 4);
    dump(impl.arguments, os, indent + 4);
    if (impl.function != nullptr) {
        print_indent(os, indent + 4);
        os << to_string(impl.function) << '\n';
    }
}

template<>
void dump(ASTNode const &n, Comptime const &impl, std::wostream &os, int indent)
{
    if (impl.statements) {
        dump(impl.statements, os, indent + 4);
    } else if (!impl.output.empty()) {
        print_indent(os, indent + 4);
        os << impl.output << '\n';
    } else {
        print_indent(os, indent + 4);
        os << impl.script_text << '\n';
    }
}

template<class StatementWrapper>
    requires std::is_same_v<StatementWrapper, DeferStatement>
    || std::is_same_v<StatementWrapper, LoopStatement>
    || std::is_same_v<StatementWrapper, Yield>
void dump(ASTNode const &n, StatementWrapper const &impl, std::wostream &os, int indent)
{
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &n, Enum const &impl, std::wostream &os, int indent)
{
    for (auto const &v : impl.values) {
        dump(v, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, Return const &impl, std::wostream &os, int indent)
{
    dump(impl.expression, os, indent + 4);
}

template<>
void dump(ASTNode const &n, ExpressionList const &impl, std::wostream &os, int indent)
{
    print_indent(os, indent);
    os << "#expressions: " << impl.expressions.size() << "\n";
    for (auto const &expr : impl.expressions) {
        dump(expr, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, ForStatement const &impl, std::wostream &os, int indent)
{
    dump(impl.range_expr, os, indent + 4);
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &n, FunctionDeclaration const &impl, std::wostream &os, int indent)
{
    for (auto const &gen : impl.generics) {
        dump(gen, os, indent + 4);
    }
    for (auto const &param : impl.parameters) {
        dump(param, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, FunctionDefinition const &impl, std::wostream &os, int indent)
{
    dump(impl.declaration, os, indent + 4);
    dump(impl.implementation, os, indent + 4);
}

template<>
void dump(ASTNode const &n, IfStatement const &impl, std::wostream &os, int indent)
{
    dump(impl.condition, os, indent + 4);
    dump(impl.if_branch, os, indent + 4);
    dump(impl.else_branch, os, indent + 4);
}

template<>
void dump(ASTNode const &n, LoopStatement const &impl, std::wostream &os, int indent)
{
    dump(impl.statement, os, indent + 4);
}

template<>
void dump(ASTNode const &n, Program const &impl, std::wostream &os, int indent)
{
    for (auto const &stmt : impl.statements) {
        dump(stmt, os, indent + 4);
    }
    auto &parser { *(n.repo) };
    for (auto &[_, mod] : parser.modules) {
        dump(mod, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, PublicDeclaration const &impl, std::wostream &os, int indent)
{
    dump(impl.declaration, os, indent + 4);
}

template<>
void dump(ASTNode const &n, Struct const &impl, std::wostream &os, int indent)
{
    for (auto const &m : impl.members) {
        dump(m, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, TagValue const &impl, std::wostream &os, int indent)
{
    if (impl.operand) {
        dump(impl.operand, os, indent + 4);
    }
    if (impl.payload) {
        dump(impl.payload, os, indent + 4);
    }
}

template<>
void dump(ASTNode const &n, UnaryExpression const &impl, std::wostream &os, int indent)
{
    dump(impl.operand, os, indent + 4);
}

template<>
void dump(ASTNode const &n, VariableDeclaration const &impl, std::wostream &os, int indent)
{
    dump(impl.initializer, os, indent + 4);
}

template<>
void dump(ASTNode const &n, WhileStatement const &impl, std::wostream &os, int indent)
{
    dump(impl.condition, os, indent + 4);
    dump(impl.statement, os, indent + 4);
}

/* ======================================================================== */

template<class N>
std::wstring to_string(ASTNode const &n, N const &impl)
{
    return L"";
}

template<class Expr>
    requires std::is_same_v<Expr, BinaryExpression>
    || std::is_same_v<Expr, UnaryExpression>
std::wstring to_string(ASTNode const &, Expr const &impl)
{
    return as_wstring(Operator_name(impl.op));
}

template<>
std::wstring to_string(ASTNode const &, BoolConstant const &impl)
{
    return (impl.value) ? L"True" : L"False";
}

template<>
std::wstring to_string(ASTNode const &n, Call const &impl)
{
    return to_string(impl.callable);
}

template<>
std::wstring to_string(ASTNode const &, Decimal const &impl)
{
    std::wstringstream os;
    os << impl.value;
    return os.str();
}

template<>
std::wstring to_string(ASTNode const &, Number const &impl)
{
    std::wstringstream os;
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
    || std::is_same_v<N, Import>
    || std::is_same_v<N, Include>
std::wstring to_string(ASTNode const &n, N const &impl)
{
    return impl.file_name;
}

template<>
std::wstring to_string(ASTNode const &n, Enum const &impl)
{
    std::wstringstream os;
    os << impl.name;
    if (impl.underlying_type != nullptr) {
        os << ": " << to_string(impl.underlying_type);
    }
    return os.str();
}

template<>
std::wstring to_string(ASTNode const &n, EnumValue const &impl)
{
    std::wstring ret = impl.label;
    if (impl.value != nullptr) {
        ret += L" = ";
        ret += to_string(impl.value);
    }
    if (impl.payload != nullptr) {
        ret += L" (";
        ret += to_string(impl.payload);
        ret += L")";
    }
    return ret;
}

template<>
std::wstring to_string(ASTNode const &n, ExternLink const &impl)
{
    return impl.link_name;
}

template<>
std::wstring to_string(ASTNode const &n, ForStatement const &impl)
{
    return impl.range_variable;
}

template<>
std::wstring to_string(ASTNode const &n, FunctionDeclaration const &impl)
{
    std::wstringstream os;
    os << impl.name;
    if (!impl.generics.empty()) {
        wchar_t sep { '<' };
        for (auto const &gen : impl.generics) {
            os << sep << get<Identifier>(gen).identifier;
            sep = ',';
        }
        os << '>';
    }
    os << ": " << to_string(impl.return_type);
    return os.str();
}

template<>
std::wstring to_string(ASTNode const &n, FunctionDefinition const &impl)
{
    return impl.name;
}

template<class N>
    requires std::is_same_v<N, Break>
    || std::is_same_v<N, Continue>
    || std::is_same_v<N, LoopStatement>
    || std::is_same_v<N, WhileStatement>
    || std::is_same_v<N, Yield>
std::wstring to_string(ASTNode const &n, N const &impl)
{
    if (impl.label) {
        return *impl.label;
    }
    return L"";
}

template<class N>
    requires std::is_same_v<N, Identifier> || std::is_same_v<N, StampedIdentifier>
std::wstring to_string(ASTNode const &, N const &impl)
{
    return impl.identifier;
}

template<>
std::wstring to_string(ASTNode const &n, Module const &impl)
{
    return impl.name;
}

template<>
std::wstring to_string(ASTNode const &n, Parameter const &impl)
{
    return std::format(L"{}: {}", impl.name, to_string(impl.type_name));
}

template<>
std::wstring to_string(ASTNode const &, QuotedString const &impl)
{
    return impl.string;
}

template<>
std::wstring to_string(ASTNode const &, CString const &impl)
{
    return as_wstring(impl.string);
}

template<>
std::wstring to_string(ASTNode const &, String const &impl)
{
    return impl.string;
}

template<>
std::wstring to_string(ASTNode const &, Struct const &impl)
{
    return impl.name;
}

template<>
std::wstring to_string(ASTNode const &, StructMember const &impl)
{
    return std::format(L"{} {}", impl.label, to_string(impl.member_type));
}

template<>
std::wstring to_string(ASTNode const &, TagValue const &impl)
{
    return std::format(L"{}={}", impl.label, impl.tag_value);
}

template<>
std::wstring to_string(ASTNode const &n, TypeSpecification const &impl)
{
    return std::visit(
        overloads {
            [](TypeNameNode const &d) -> std::wstring {
                auto ret { d.name };
                if (!d.arguments.empty()) {
                    wchar_t sep = '<';
                    for (auto const &arg : d.arguments) {
                        ret += sep;
                        sep = ',';
                        ret += to_string(arg);
                    }
                    ret += '>';
                }
                return ret;
            },
            [](ReferenceDescriptionNode const &d) -> std::wstring {
                return std::format(L"&{}", to_string(d.referencing));
            },
            [](SliceDescriptionNode const &d) -> std::wstring {
                return std::format(L"[]{}", to_string(d.slice_of));
            },
            [](ZeroTerminatedArrayDescriptionNode const &d) -> std::wstring {
                return std::format(L"[0]{}", to_string(d.array_of));
            },
            [](ArrayDescriptionNode const &d) -> std::wstring {
                return std::format(L"[{}]{}", d.size, to_string(d.array_of));
            },
            [](DynArrayDescriptionNode const &d) -> std::wstring {
                return std::format(L"[*]{}", to_string(d.array_of));
            },
            [](OptionalDescriptionNode const &d) -> std::wstring {
                return std::format(L"{}?", to_string(d.optional_of));
            },
            [](ResultDescriptionNode const &d) -> std::wstring {
                return std::format(L"{}/{}", to_string(d.success), to_string(d.error));
            },
        },
        impl.description);
}

template<>
std::wstring to_string(ASTNode const &, VariableDeclaration const &impl)
{
    std::wstringstream os;
    if (impl.is_const) {
        os << "const ";
    }
    os << impl.name;
    if (impl.type_name != nullptr) {
        os << ": " << to_string(impl.type_name);
    }
    return os.str();
}

/* ======================================================================== */

std::wstring to_string(ASTNode const &node)
{
    if (node == nullptr) {
        return L"[null]";
    }
    std::wstringstream os;
    os << SyntaxNodeType_name(node->type()) << ' ';
    std::visit(
        [&node, &os](auto const &impl) {
            os << to_string(node, impl);
        },
        node->node);
    if (node->bound_type != nullptr) {
        os << " -> " << node->bound_type->name;
    }
    os << " ";
    switch (node->status) {
    case ASTStatus::Initialized:
        os << L"Initialized";
        break;
    case ASTStatus::Normalized:
        os << L"Normalized";
        break;
    case ASTStatus::Bound:
        os << L"Bound";
        break;
    case ASTStatus::Undetermined:
        os << L"Undetermined";
        break;
    case ASTStatus::Ambiguous:
        os << L"Ambiguous";
        break;
    case ASTStatus::BindErrors:
        os << L"BindErrors";
        break;
    case ASTStatus::InternalError:
        os << L"InternalError";
        break;
    }
    return os.str();
}

void header(ASTNode const &node, std::wostream &os)
{
    os << node.id.value()
       << " "
       << " (" << node->location.index
       << ".." << node->location.index + node->location.length
       << ") "
       << to_string(node);
}

void dump(ASTNode const &node, std::wostream &os, int indent)
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
        os << ' ' << node->ns->types.size() << "/"
           << node->ns->functions.size() << "/"
           << node->ns->variables.size();
    }
    os << std::endl;
    std::visit(
        [&node, &indent, &os](auto const &impl) {
            dump(node, impl, os, indent);
        },
        node->node);
}

void dump(ASTNodes const &nodes, std::wostream &os, int indent)
{
    for (auto const &node : nodes) {
        dump(node, os, indent);
    }
}

}
