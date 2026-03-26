/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <format>
#include <string>
#include <string_view>

#include <Util/Defer.h>
#include <Util/IO.h>
#include <Util/Lexer.h>
#include <Util/Logging.h>
#include <Util/Options.h>
#include <Util/TokenLocation.h>
#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>
#include <type_traits>

namespace Lia {

using namespace Util;

using NodeMap = std::map<std::wstring, ASTNode>;

struct Parser {

    struct LiaComptimeBlock {
        constexpr static wchar_t const *begin = L"@comptime";
        constexpr static wchar_t const *end = L"@end";
    };

    using LiaLexerTypes = LexerTypes<std::wstring_view, wchar_t, LiaKeyword>;
    using LiaLexer = Lexer<
        LiaLexerTypes,
        LiaLexerTypes::ScannerPack<
            LiaLexerTypes::CScannerPack,
            LiaLexerTypes::QuotedStringScanner<LiaLexerTypes::DefaultQuotes>,
            LiaLexerTypes::RawScanner<LiaComptimeBlock>>>;
    using Token = LiaLexer::Token;
    using LexerError = LiaLexer::LexerError;
    using LexerResult = LiaLexer::LexerResult;
    using OperatorSymbol = std::variant<wchar_t, LiaKeyword>;

    struct OperatorDef {
        Operator       op;
        OperatorSymbol sym;
        Precedence     precedence;
        Position       position { Position::Infix };
        Associativity  associativity { Associativity::Left };
    };

    enum class ParseLevel {
        Module,
        Function,
        Block,
    };

    static std::vector<OperatorDef> operators;
    std::wstring_view               text;
    LiaLexer                        lexer {};
    ParseLevel                      level { ParseLevel::Module };
    std::vector<ASTNodeImpl>        nodes;
    std::vector<Namespace>          namespace_nodes;
    std::vector<LiaError>           errors;
    std::vector<ASTNode>            unbound_nodes;
    std::vector<NSNode>             namespaces;
    std::vector<ASTNode>            node_stack;
    NodeMap                         modules {};
    ASTNode                         program;
    int                             pass { 0 };
    int                             unbound { 0 };

    size_t             size() const;
    bool               empty() const;
    ASTNodeImpl const &operator[](size_t ix) const;
    ASTNodeImpl       &operator[](size_t ix);
    size_t             hunt(size_t ix) const;
    size_t             hunt(ASTNode const &n) const;

    template<class N, typename... Args>
    ASTNode make_node(TokenLocation loc, Args... args)
    {
        nodes.push_back(ASTNodeImpl::make<N>(args...));
        ASTNode ret = { this };
        auto   &n = nodes.back();
        n.id = ret;
        // ASTNode ret = this->make_node<N>(args...);
        n.location = std::move(loc);
        return ret;
    }

    template<class N, typename... Args>
    ASTNode make_node(Args... args)
    {
        nodes.push_back(ASTNodeImpl::make<N>(args...));
        ASTNode ret = { this };
        nodes.back().id = ret;
        trace(L"[C] {}", ret);
        return ret;
    }

    ASTNode add_node(SyntaxNode impl)
    {
        nodes.push_back(ASTNodeImpl { std::move(impl) });
        ASTNode ret = { this };
        nodes.back().id = ret;
        trace(L"[C] {}", ret);
        return ret;
    }

    ASTNode add_node(TokenLocation loc, SyntaxNode impl)
    {
        nodes.push_back(ASTNodeImpl { std::move(impl) });
        ASTNode ret = { this };
        nodes.back().id = ret;
        nodes.back().location = std::move(loc);
        trace(L"[C] {}", ret);
        return ret;
    }

    template<class N>
    ASTNode copy_node(TokenLocation loc, N impl)
    {
        nodes.push_back(ASTNodeImpl::make<N>(std::move(impl)));
        ASTNode ret = { this };
        nodes.back().id = ret;
        nodes.back().location = std::move(loc);
        trace(L"[C] {}", ret);
        return ret;
    }

    Parser();

    Token                      parse_statements(ASTNodes &statements);
    ASTNode                    parse_statement();
    ASTNode                    parse_module_level_statement();
    BindResult                 bind(ASTNode node = nullptr);
    std::wstring_view          text_at(size_t start, std::optional<size_t> end) const;
    std::wstring_view          text_of(Token const &token) const;
    std::wstring_view          text_of(LexerErrorMessage const &error) const;
    std::wstring_view          text_of(LexerError const &error) const;
    std::wstring_view          text_of(LexerResult const &res) const;
    std::wstring_view          text_of(TokenLocation const &location) const;
    ASTNode                    parse_primary();
    ASTNode                    parse_expression(Precedence min_prec = 0);
    bool                       check_op();
    std::optional<OperatorDef> check_binop();
    std::optional<OperatorDef> check_prefix_op();
    std::optional<OperatorDef> check_postfix_op();
    ASTNode                    parse_type();
    ASTNode                    parse_braced_initializer();
    ASTNode                    parse_break_continue();
    ASTNode                    parse_defer();
    ASTNode                    parse_embed();
    ASTNode                    parse_enum();
    ASTNode                    parse_for();
    ASTNode                    parse_func();
    ASTNode                    parse_if();
    ASTNode                    parse_import();
    ASTNode                    parse_include();
    ASTNode                    parse_loop();
    ASTNode                    parse_public();
    ASTNode                    parse_return();
    ASTNode                    parse_struct();
    ASTNode                    parse_var_decl();
    ASTNode                    parse_while();
    ASTNode                    parse_yield();
    pType                      type_of(Strings const &name) const;
    bool                       has_function(Strings const &name, pType const &type) const;
    ASTNode                    find_function(Strings const &name, pType const &type) const;
    ASTNode                    find_function_by_arg_list(std::wstring const &name, pType const &type) const;
    ASTNodes                   find_overloads(Strings const &name, ASTNodes const &type_args) const;
    void                       register_variable(std::wstring name, ASTNode node);
    bool                       has_variable(Strings const &name) const;
    void                       register_function(std::wstring name, ASTNode node);
    pType                      find_type(Strings const &name) const;
    ASTNode                    current_function() const;
    void                       register_type(std::wstring name, pType type);
    void                       clear_namespaces();
    void                       push_namespace(ASTNode const &n);
    void                       pop_namespace(ASTNode const &n);

    void append(LexerErrorMessage const &lexer_error);
    void append(LexerErrorMessage const &lexer_error, char const *message);
    void append(LexerErrorMessage const &lexer_error, wchar_t const *message);
    void append(Token const &token, char const *message);
    void append(Token const &token, wchar_t const *message);
    void append(TokenLocation location, std::wstring message);
    void append(TokenLocation location, std::string const &message);
    void append(TokenLocation location, wchar_t const *message);
    void append(TokenLocation location, char const *message);

    template<typename... Args>
    void append(Token const &token, std::format_string<Args...> const message, Args &&...args)
    {
        append(token.location, std::vformat(message.get(), std::make_format_args(args...)));
    }

    template<typename... Args>
    void append(Token const &token, std::wformat_string<Args...> const message, Args &&...args)
    {
        append(token.location, std::vformat(message.get(), std::make_wformat_args(args...)));
    }

    template<typename... Args>
    void append(TokenLocation location, std::format_string<Args...> const message, Args &&...args)
    {
        append(std::move(location), std::vformat(message.get(), std::make_format_args(args...)));
    }

    template<typename... Args>
    void append(TokenLocation location, std::wformat_string<Args...> const message, Args &&...args)
    {
        append(std::move(location), std::vformat(message.get(), std::make_wformat_args(args...)));
    }

    template<typename... Args>
    void append(LexerErrorMessage const &lexer_error, std::format_string<Args...> const message, Args &&...args)
    {
        append(lexer_error.location, std::vformat(message.get(), std::make_format_args(args...)));
    }

    template<typename... Args>
    void append(LexerErrorMessage const &lexer_error, std::wformat_string<Args...> const message, Args &&...args)
    {
        append(lexer_error.location, std::vformat(message.get(), std::make_wformat_args(args...)));
    }

    BindError bind_error(LiaError const &error);
    BindError bind_error(TokenLocation location, std::wstring const &msg);
    BindError bind_error(TokenLocation location, std::string const &msg);

    template<typename... Args>
    BindError bind_error(TokenLocation location, std::format_string<Args...> const message, Args &&...args)
    {
        return bind_error(std::move(location), as_wstring(std::vformat(message.get(), std::make_format_args(args...))));
    }

    template<typename... Args>
    BindError bind_error(TokenLocation location, std::wformat_string<Args...> const message, Args &&...args)
    {
        return bind_error(std::move(location), std::vformat(message.get(), std::make_wformat_args(args...)));
    }
};

template<typename Node, typename... Args>
ASTNode make_node(ASTNode from, Args... args)
{
    ASTNode ret = from.repo->make_node<Node>(from->location, args...);
    if (from->ns != nullptr) {
        ret->ns = from->ns;
    }
    ret->supercedes = from;
    from->superceded_by = ret;
    return ret;
}

ASTNode add_node(ASTNode from, SyntaxNode impl);

template<typename Node>
ASTNode copy_node(ASTNode from, Node impl)
{
    auto ret = from.repo->copy_node<Node>(from->location, std::move(impl));
    if (from->ns != nullptr) {
        ret->ns = from->ns;
    }
    return ret;
}

template<typename Node>
    requires is_component<Node> || std::is_same_v<Node, Block>
ASTNode parse(Parser &parser, std::wstring const &text, std::string_view name = "")
{
    parser.text = text;
    parser.lexer.push_source(text);

    ASTNode ret;
    auto    old_level = parser.level;
    if constexpr (std::is_same_v<Node, Block>) {
        parser.level = Parser::ParseLevel::Block;
        ret = parser.make_node<Node>();
    } else {
        assert(!name.empty());
        ret = parser.make_node<Node>(as_wstring(name), text);
    }
    ASTNodes statements;
    if (auto t = parser.parse_statements(statements); !t.matches(TokenKind::EndOfFile)) {
        parser.append(t, "Expected end of file");
        parser.level = old_level;
        return nullptr;
    }
    if (!statements.empty()) {
        ret->location = statements[0]->location + statements.back()->location;
        std::visit(
            overloads {
                [&statements, &parser, &ret](Program &n) {
                    n.statements = std::move(statements);
                    parser.program = ret;
                },
                [&statements, &parser, &ret, &name](Module &n) {
                    n.statements = std::move(statements);
                    parser.modules[as_wstring(name)] = ret;
                },
                [&statements](Block &n) {
                    n.statements = std::move(statements);
                },
                [](auto &n) { UNREACHABLE(); } },
            ret->node);
        parser.level = old_level;
        return ret;
    }
    parser.level = old_level;
    return nullptr;
}

}
