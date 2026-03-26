/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include <Util/StringUtil.h>

#include <App/Config.h>
#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>

namespace Lia {

namespace fs = std::filesystem;
using namespace std::literals;

template<class N>
ASTNode normalize(ASTNode n, N const &impl)
{
    return n;
}

template<>
ASTNode normalize(ASTNode n, BinaryExpression const &impl)
{
    auto make_expression_list = [n]() -> ASTNode {
        ASTNodes                     nodes;
        std::function<void(ASTNode)> flatten;
        flatten = [&nodes, &flatten](ASTNode n) {
            if (auto const binary_expr = std::get_if<BinaryExpression>(&n->node); binary_expr != nullptr) {
                auto binexp = *binary_expr;
                if (binexp.op == Operator::Sequence) {
                    flatten(binexp.lhs);
                    nodes.push_back(normalize(binexp.rhs));
                } else {
                    nodes.push_back(normalize(n));
                }
            } else {
                nodes.push_back(normalize(n));
            }
        };
        flatten(n);
        return make_node<ExpressionList>(n, nodes);
    };

    auto make_name_list = [](ASTNode const &n) -> std::expected<ASTNode, LiaError> {
        ASTNodes                                                 nodes;
        std::function<std::expected<ASTNode, LiaError>(ASTNode)> flatten;
        flatten = [&nodes, &flatten](ASTNode n) -> std::expected<ASTNode, LiaError> {
            if (is<BinaryExpression>(n)) {
                auto const &binexp { get<BinaryExpression>(n) };
                if (binexp.op != Operator::MemberAccess) {
                    return std::unexpected(LiaError { n->location, L"Expected dotted identifier list" });
                }
                if (auto res = flatten(binexp.lhs); !res) {
                    return std::unexpected(res.error());
                }
                if (auto res = flatten(binexp.rhs); !res) {
                    return std::unexpected(res.error());
                }
            } else if (is<Identifier>(n)) {
                nodes.push_back(normalize(n));
            } else {
                return std::unexpected(LiaError { n->location, L"Expected dotted identifier list" });
            }
            return n;
        };
        if (is<Identifier>(n)) {
            return normalize(n);
        }
        if (auto res = flatten(n); !res) {
            return std::unexpected(res.error());
        }
        return make_node<ExpressionList>(n, nodes);
    };

    switch (impl.op) {
    case Operator::Call: {
        auto arg_list = normalize(impl.rhs);
        if (is<Void>(arg_list)) {
            arg_list = make_node<ExpressionList>(arg_list, ASTNodes {});
        }
        if (!is<ExpressionList>(arg_list)) {
            auto &parser { *(arg_list.repo) };
            arg_list = parser.make_node<ExpressionList>(ASTNodes { arg_list });
        }
        assert(is<ExpressionList>(arg_list));
        arg_list = normalize(arg_list);
        if (auto res = make_name_list(impl.lhs); !res) {
            n.bind_error(res.error());
            return {};
        } else {
            auto call = make_node<Call>(n, res.value(), arg_list);
            return normalize(call);
        }
    }
    case Operator::Sequence:
        return make_expression_list();
    case Operator::MemberAccess: {
        auto aggregate { normalize(impl.lhs) };
        auto member { normalize(impl.rhs) };
        if (is<QuotedString>(member)) {
            auto const &qs { get<QuotedString>(member) };
            member = make_node<Identifier>(member, qs.string.substr(1, qs.string.length() - 2));
        }
        return make_node<BinaryExpression>(n, aggregate, impl.op, member);
    } break;
    case Operator::Range:
        return make_node<BinaryExpression>(n, normalize(impl.lhs), impl.op, normalize(impl.rhs));
    default:
        if (assign_ops.contains(impl.op)) {
            auto const bin_expr = make_node<BinaryExpression>(
                n,
                normalize(impl.lhs),
                assign_ops[impl.op],
                normalize(impl.rhs));
            return make_node<BinaryExpression>(n, impl.lhs, Operator::Assign, normalize(bin_expr));
        }
        auto normalized = make_node<BinaryExpression>(n, normalize(impl.lhs), impl.op, normalize(impl.rhs));
        if (auto folded = fold(normalized); folded != nullptr) {
            return folded;
        }
        return normalized;
    }
}

template<>
ASTNode normalize(ASTNode n, Block const &impl)
{
    const_cast<ASTNode &>(n)->init_namespace();
    auto ret = make_node<Block>(n, normalize(impl.statements));
    return ret;
}

template<>
ASTNode normalize(ASTNode n, Comptime const &impl)
{
    Parser &parser = *(n.repo);
    auto    script = parse<Block>(parser, impl.script_text);

    if (!parser.errors.empty()) {
        log_error("Syntax error(s) found in @comptime block:");
        for (auto const &err : parser.errors) {
            log_error(L"{}:{} {}", err.location.line + 1, err.location.column + 1, err.message);
        }
        return n;
    }

    auto synthetic_return_type = parser.make_node<TypeSpecification>(
        n->location,
        TypeNameNode { { L"string" }, ASTNodes {} });
    auto synthetic_decl = parser.make_node<FunctionDeclaration>(
        n->location,
        std::format(L"comptime-{}", *(n.id)),
        ASTNodes {},
        ASTNodes {},
        synthetic_return_type);
    auto synthetic_def = parser.make_node<FunctionDefinition>(
        std::format(L"comptime-{}", *(n.id)),
        synthetic_decl,
        script);
    normalize(synthetic_def);

    script = normalize(synthetic_def);
    trace("@comptime block parsed");
    return make_node<Comptime>(n, impl.script_text, script);
}

template<>
ASTNode normalize(ASTNode n, DeferStatement const &impl)
{
    return make_node<DeferStatement>(n, normalize(impl.statement));
}

template<>
ASTNode normalize(ASTNode n, Embed const &impl)
{
    auto fname = as_utf8(impl.file_name);
    if (auto contents_maybe = read_file_by_name<wchar_t>(fname); contents_maybe.has_value()) {
        info(L"Embedding `{}`", impl.file_name);
        auto const &contents = contents_maybe.value();
        return make_node<QuotedString>(n, contents, QuoteType::DoubleQuote);
    } else {
        n.error("Could not open `{}`: {}", fname, contents_maybe.error().to_string());
        return nullptr;
    }
}

template<>
ASTNode normalize(ASTNode n, EnumValue const &impl)
{
    return make_node<EnumValue>(n, impl.label, normalize(impl.value), normalize(impl.payload));
}

ASTNode normalize(ASTNode n, Enum const &impl)
{
    return make_node<Enum>(n, impl.name, normalize(impl.underlying_type), normalize(impl.values));
}

template<>
ASTNode normalize(ASTNode n, ExpressionList const &impl)
{
    return make_node<ExpressionList>(n, normalize(impl.expressions));
}

template<>
ASTNode normalize(ASTNode n, ForStatement const &impl)
{
    n->init_namespace();
    auto range_expr = normalize(impl.range_expr);
    n->ns->register_variable(impl.range_variable, get<BinaryExpression>(range_expr).lhs);
    return make_node<ForStatement>(n, impl.range_variable, range_expr, normalize(impl.statement));
}

template<>
ASTNode normalize(ASTNode n, FunctionDeclaration const &impl)
{
    return make_node<FunctionDeclaration>(
        n,
        impl.name,
        normalize(impl.generics),
        normalize(impl.parameters),
        normalize(impl.return_type));
}

template<>
ASTNode normalize(ASTNode n, FunctionDefinition const &impl)
{
    n->init_namespace();
    auto ret = make_node<FunctionDefinition>(n, impl.name, normalize(impl.declaration), normalize(impl.implementation));
    return ret;
}

template<>
ASTNode normalize(ASTNode n, IfStatement const &impl)
{
    return make_node<IfStatement>(
        n,
        normalize(impl.condition),
        normalize(impl.if_branch),
        normalize(impl.else_branch));
}

template<>
ASTNode normalize(ASTNode n, Import const &impl)
{
    assert(!impl.file_name.empty());
    auto     fname { join(impl.file_name, L"/"sv) };
    fs::path path {};
    for (auto const &elem : impl.file_name) {
        path += elem;
    }
    path.concat(".lia");
    if (!fs::exists(path)) {
        path = lia_dir() / "share" / path;
    }
    Parser &parser { *(n.repo) };

    NSNode &ns { parser.namespaces.back() };
    ASTNode proxy;
    for (auto const &elem : impl.file_name) {
        if (proxy = ns->find_module(elem); proxy != nullptr) {
            ns = proxy->ns;
        } else if (!ns->contains(elem)) {
            proxy = parser.make_node<ModuleProxy>(n->location, elem);
            proxy->init_namespace();
            parser.pop_namespace(proxy);
            ns->register_module(elem, proxy);
            ns = proxy->ns;
        }
    }

    ASTNode module { nullptr };
    for (auto const &[name, m] : parser.modules) {
        if (name == fname) {
            module = m;
            break;
        }
    }

    if (module == nullptr) {
        if (auto contents_maybe = read_file_by_name<wchar_t>(path.string()); contents_maybe.has_value()) {
            info("Importing module `{}`", path.string());
            auto const &contents = contents_maybe.value();
            module = parse<Module>(parser, std::move(contents), path.string());
            get<ModuleProxy>(proxy).module = module;
        } else {
            n.error(L"Could not open import `{}`", impl.file_name);
        }
    }
    if (module) {
        return proxy;
    }
    return n;
}

template<>
ASTNode normalize(ASTNode n, Include const &impl)
{
    auto fname = as_utf8(impl.file_name);
    if (auto contents_maybe = read_file_by_name<wchar_t>(fname); contents_maybe.has_value()) {
        info(L"Including `{}`", impl.file_name);
        auto const &contents = contents_maybe.value();
        auto        node = parse<Block>(*(n.repo), std::move(contents), fname);
        if (node) {
            node->location = n->location;
            return normalize(node);
        }
    } else {
        n.error(L"Could not open include file `{}`", impl.file_name);
    }
    return nullptr;
}

template<>
ASTNode normalize(ASTNode n, LoopStatement const &impl)
{
    return make_node<LoopStatement>(n, impl.label, normalize(impl.statement));
}

template<>
ASTNode normalize(ASTNode n, Module const &impl)
{
    if (n->ns == nullptr) {
        n->init_namespace();
    }
    return make_node<Module>(n, impl.name, impl.source, normalize(impl.statements));
}

template<>
ASTNode normalize(ASTNode n, Parameter const &impl)
{
    return make_node<Parameter>(n, impl.name, normalize(impl.type_name));
}

template<>
ASTNode normalize(ASTNode n, Program const &impl)
{
    Parser &parser = *(n.repo);
    assert(parser.program == n);
    n->init_namespace();
    for (auto const &t : TypeRegistry::the().types) {
        n->ns->register_type(t.name, t.id);
    }
    auto statements = normalize(impl.statements);
    auto again { true };
    while (again) {
        again = false;
        for (auto &[name, mod] : parser.modules) {
            auto const normalized = normalize(mod);
            if (normalized != mod) {
                parser.modules[name] = normalized;
                again = true;
            }
        }
    }
    auto ret = make_node<Program>(n, impl.name, statements);
    return ret;
}

template<>
ASTNode normalize(ASTNode n, PublicDeclaration const &impl)
{
    return make_node<PublicDeclaration>(n, impl.name, normalize(impl.declaration));
}

template<>
ASTNode normalize(ASTNode n, QuotedString const &impl)
{
    auto unescape = [](auto const &s) -> std::wstring {
        std::wstring escaped;
        bool         escape { false };
        for (auto const ch : s.substr(0, s.length() - 1).substr(1)) {
            if (escape) {
                switch (ch) {
                case 'n':
                    escaped += '\n';
                    break;
                case 'r':
                    escaped += '\r';
                    break;
                case 't':
                    escaped += '\t';
                    break;
                default:
                    escaped += ch;
                }
                escape = false;
            } else {
                if (ch == '\\') {
                    escape = true;
                } else {
                    escaped += ch;
                }
            }
        }
        return escaped;
    };

    switch (impl.quote_type) {
    case QuoteType::DoubleQuote:
        return make_node<String>(n, unescape(impl.string));
    case QuoteType::BackQuote:
        return make_node<CString>(n, as_utf8(unescape(impl.string)));
    case QuoteType::SingleQuote:
        return make_node<Number>(n, static_cast<uint64_t>(impl.string[1]));
    default:
        UNREACHABLE();
    }
}

template<>
ASTNode normalize(ASTNode n, Return const &impl)
{
    return make_node<Return>(n, normalize(impl.expression));
}

template<>
ASTNode normalize(ASTNode n, StructMember const &impl)
{
    return make_node<StructMember>(n, impl.label, normalize(impl.member_type));
}

template<>
ASTNode normalize(ASTNode n, Struct const &impl)
{
    return make_node<Struct>(n, impl.name, normalize(impl.members));
}

template<>
ASTNode normalize(ASTNode n, TypeSpecification const &impl)
{
    auto description = std::visit(
        overloads {
            [](TypeNameNode const &d) -> TypeSpecificationDescription {
                return TypeNameNode { d.name, normalize(d.arguments) };
            },
            [](ReferenceDescriptionNode const &d) -> TypeSpecificationDescription {
                return ReferenceDescriptionNode { normalize(d.referencing) };
            },
            [](SliceDescriptionNode const &d) -> TypeSpecificationDescription {
                return SliceDescriptionNode { normalize(d.slice_of) };
            },
            [](ZeroTerminatedArrayDescriptionNode const &d) -> TypeSpecificationDescription {
                return ZeroTerminatedArrayDescriptionNode { normalize(d.array_of) };
            },
            [](ArrayDescriptionNode const &d) -> TypeSpecificationDescription {
                return ArrayDescriptionNode { normalize(d.array_of), d.size };
            },
            [](DynArrayDescriptionNode const &d) -> TypeSpecificationDescription {
                return DynArrayDescriptionNode { normalize(d.array_of) };
            },
            [](OptionalDescriptionNode const &d) -> TypeSpecificationDescription {
                return OptionalDescriptionNode { normalize(d.optional_of) };
            },
            [](ResultDescriptionNode const &d) -> TypeSpecificationDescription {
                return ResultDescriptionNode {
                    normalize(d.success),
                    normalize(d.error),
                };
            },
        },
        impl.description);
    return n;
}

template<>
ASTNode normalize(ASTNode n, UnaryExpression const &impl)
{
    auto normalized { make_node<UnaryExpression>(n, impl.op, normalize(impl.operand)) };
    if (auto folded { fold(normalized) }; folded != nullptr) {
        return folded;
    }
    return normalized;
}

template<>
ASTNode normalize(ASTNode n, VariableDeclaration const &impl)
{
    normalize(impl.type_name);
    normalize(impl.initializer);
    return n;
}

template<>
ASTNode normalize(ASTNode n, WhileStatement const &impl)
{
    return make_node<WhileStatement>(n, impl.label, normalize(impl.condition), normalize(impl.statement));
}

template<>
ASTNode normalize(ASTNode n, Yield const &impl)
{
    return make_node<Yield>(n, impl.label, normalize(impl.statement));
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
        char const *t = SyntaxNodeType_name(node->type());
        ret = std::visit(
            [&node](auto impl) {
                return normalize(node, impl);
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
    ASTNodes normalized {};
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
}
