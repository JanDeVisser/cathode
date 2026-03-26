/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

// #define DEBUG_NAMESPACE_STACK

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string_view>

#include <Util/Defer.h>
#include <Util/Lexer.h>
#include <Util/Logging.h>
#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

namespace Lia {

using namespace std::literals;
using namespace Util;

std::vector<Parser::OperatorDef> Parser::operators {
    { Operator::Add, '+', 11 },
    { Operator::AddressOf, '&', 14, Position::Prefix, Associativity::Right },
    { Operator::Apply, LiaKeyword::Apply, 2 },
    { Operator::Assign, '=', 2, Position::Infix, Associativity::Right },
    { Operator::AssignAnd, LiaKeyword::AssignAnd, 1, Position::Infix, Associativity::Right },
    { Operator::AssignDecrement, LiaKeyword::AssignDecrement, 1, Position::Infix, Associativity::Right },
    { Operator::AssignDivide, LiaKeyword::AssignDivide, 1, Position::Infix, Associativity::Right },
    { Operator::AssignIncrement, LiaKeyword::AssignIncrement, 1, Position::Infix, Associativity::Right },
    { Operator::AssignModulo, LiaKeyword::AssignModulo, 1, Position::Infix, Associativity::Right },
    { Operator::AssignMultiply, LiaKeyword::AssignMultiply, 1, Position::Infix, Associativity::Right },
    { Operator::AssignOr, LiaKeyword::AssignOr, 1, Position::Infix, Associativity::Right },
    { Operator::AssignShiftLeft, LiaKeyword::AssignShiftLeft, 1, Position::Infix, Associativity::Right },
    { Operator::AssignShiftRight, LiaKeyword::AssignShiftRight, 1, Position::Infix, Associativity::Right },
    { Operator::AssignXor, LiaKeyword::AssignXor, 1, Position::Infix, Associativity::Right },
    { Operator::BinaryInvert, '~', 14, Position::Prefix, Associativity::Right },
    { Operator::Call, '(', 15 },
    { Operator::Call, ')', 15, Position::Closing },
    { Operator::Cast, LiaKeyword::Cast, 14 },
    { Operator::Divide, '/', 12 },
    { Operator::Equals, LiaKeyword::Equals, 8 },
    { Operator::Greater, '>', 8 },
    { Operator::GreaterEqual, LiaKeyword::GreaterEqual, 8 },
    { Operator::Idempotent, '+', 14, Position::Prefix, Associativity::Right },
    { Operator::Length, '#', 9, Position::Prefix, Associativity::Right },
    { Operator::Less, '<', 8 },
    { Operator::LessEqual, LiaKeyword::LessEqual, 8 },
    { Operator::LogicalAnd, LiaKeyword::LogicalAnd, 5 },
    { Operator::LogicalInvert, '!', 14, Position::Prefix, Associativity::Right },
    { Operator::LogicalOr, LiaKeyword::LogicalOr, 4 },
    { Operator::MemberAccess, '.', 15 },
    { Operator::Modulo, '%', 12 },
    { Operator::Multiply, '*', 12 },
    { Operator::Negate, '-', 14, Position::Prefix, Associativity::Right },
    { Operator::NotEqual, LiaKeyword::NotEqual, 8 },
    { Operator::Range, LiaKeyword::Range, 2 },
    { Operator::Sequence, ',', 1 },
    { Operator::ShiftLeft, LiaKeyword::ShiftLeft, 10 },
    { Operator::ShiftRight, LiaKeyword::ShiftRight, 10 },
    { Operator::Sizeof, LiaKeyword::Sizeof, 9, Position::Prefix, Associativity::Right },
    { Operator::Subscript, '[', 15, Position::Postfix },
    { Operator::Subscript, ']', 15, Position::Closing },
    { Operator::Subtract, '-', 11 },
    { Operator::Unwrap, LiaKeyword::Must, 14, Position::Prefix, Associativity::Right },
    { Operator::UnwrapError, LiaKeyword::Error, 14, Position::Prefix, Associativity::Right },
};

struct BindingPower {
    int left;
    int right;
};

static BindingPower binding_power(Parser::OperatorDef op)
{
    switch (op.position) {
    case Position::Infix:
        switch (op.associativity) {
        case Associativity::Left:
            return { op.precedence * 2 - 1, op.precedence * 2 };
        case Associativity::Right:
            return { op.precedence * 2, op.precedence * 2 - 1 };
        }
    case Position::Prefix:
        return { -1, op.precedence * 2 - 1 };
    case Position::Postfix:
        return { op.precedence * 2 - 1, -1 };
    case Position::Closing:
        return { -1, -1 };
    default:
        UNREACHABLE();
    }
}

Parser::Parser()
{
}

size_t Parser::size() const
{
    return nodes.size();
}

bool Parser::empty() const
{
    return nodes.empty();
}

ASTNodeImpl const &Parser::operator[](size_t ix) const
{
    return nodes[hunt(ix)];
}

ASTNodeImpl &Parser::operator[](size_t ix)
{
    return nodes[hunt(ix)];
}

size_t Parser::hunt(size_t ix) const
{
    while (nodes[ix].superceded_by != nullptr) {
        ix = nodes[ix].superceded_by.id.value();
    }
    return ix;
}

size_t Parser::hunt(ASTNode const &n) const
{
    assert(n.id.has_value());
    return hunt(n.id.value());
}

Parser::Token Parser::parse_statements(ASTNodes &statements)
{
    while (true) {
        auto const t = lexer.peek();
        if (t.matches(TokenKind::EndOfFile) || t.matches_symbol('}')) {
            lexer.lex();
            return t;
        }
        if (auto stmt = (level == ParseLevel::Module) ? parse_module_level_statement() : parse_statement(); stmt != nullptr) {
            statements.push_back(stmt);
        }
    }
}

ASTNode Parser::parse_module_level_statement()
{
    auto t = lexer.peek();
    // std::cerr << TokenKind_name(t.kind) << " " << std::endl;
    switch (t.kind) {
    case TokenKind::EndOfFile:
        append(t, "Unexpected end of file");
        return {};
    case TokenKind::Identifier:
        lexer.lex();
        if (auto err = lexer.expect_symbol(':'); !err.has_value()) {
            append(err.error(), "Expected variable declaration");
            break;
        }
        return parse_statement();
    case TokenKind::Keyword: {
        switch (t.keyword()) {
        case LiaKeyword::Const:
            lexer.lex();
            return parse_module_level_statement();
        case LiaKeyword::Enum:
            return parse_enum();
        case LiaKeyword::Func:
            return parse_func();
        case LiaKeyword::Import:
            return parse_import();
        case LiaKeyword::Include:
            return parse_include();
        case LiaKeyword::Public:
            return parse_public();
        case LiaKeyword::Struct:
            return parse_struct();
        default:
            break;
        }
    } break;
    default:
        break;
    }
    lexer.lex();
    append(t, L"Unexpected token `{}`", text_of(t));
    return {};
}

ASTNode Parser::parse_statement()
{
    auto t = lexer.peek();
    // trace("parse_statement() t = {} [{}]", as_utf8(text_of(t)), TokenKind_name(t.kind));
    switch (t.kind) {
    case TokenKind::EndOfFile:
        append(t, "Unexpected end of file");
        return {};
    case TokenKind::Identifier:
        if (lexer.has_lookback(1) && lexer.lookback(0).matches_symbol(':') && lexer.lookback(1).matches(TokenKind::Identifier)) {
            // This is the type of a variable decl:
            return parse_var_decl();
        }
        lexer.lex();
        if (lexer.peek().matches_symbol(':')) {
            lexer.lex();
            return parse_statement();
        }
        lexer.push_back(t);
        // Fall through
    case TokenKind::Number:
    case TokenKind::QuotedString:
        return parse_expression();
    case TokenKind::Keyword: {
        switch (t.keyword()) {
        case LiaKeyword::Break:
        case LiaKeyword::Continue:
            return parse_break_continue();
        case LiaKeyword::Const:
            lexer.lex();
            return parse_statement();
        case LiaKeyword::Defer:
            return parse_defer();
        case LiaKeyword::Embed:
            return parse_embed();
        case LiaKeyword::Enum:
            return parse_enum();
        case LiaKeyword::For:
            return parse_for();
        case LiaKeyword::Func:
            return parse_func();
        case LiaKeyword::If:
            return parse_if();
        case LiaKeyword::Include:
            return parse_include();
        case LiaKeyword::Loop:
            return parse_loop();
        case LiaKeyword::Public:
            return parse_public();
        case LiaKeyword::Return:
            return parse_return();
        case LiaKeyword::Struct:
            return parse_struct();
        case LiaKeyword::While:
            return parse_while();
        case LiaKeyword::Yield:
            return parse_yield();
        default:
            append(t, "Unexpected keyword `{}` parsing statement", as_utf8(text_of(t)));
            lexer.lex();
            return {};
        }
    } break;
    case TokenKind::Symbol:
        switch (t.symbol_code()) {
        case ';':
            return make_node<Dummy>(lexer.lex().location);
        case '{': {
            lexer.lex();
            ASTNodes block;
            auto     old_level = level;
            level = ParseLevel::Block;
            if (auto const end_token = parse_statements(block); !end_token.matches_symbol('}')) {
                append(t, "Unexpected end of block");
                level = old_level;
                return {};
            } else {
                if (block.empty()) {
                    level = old_level;
                    return make_node<Void>(t.location + end_token.location);
                }
                level = old_level;
                return make_node<Block>(t.location + end_token.location, block);
            }
            level = old_level;
        }
        case '=':
        case '?':
        case '&':
        case '[':
            if (lexer.has_lookback(1)
                && lexer.lookback(0).matches_symbol(':')
                && lexer.lookback(1).matches(TokenKind::Identifier)) {
                // This is the '=' of a variable decl with implied type:
                //   or a '?' of an optional type
                //   or a '&' of a reference type
                //   or a '[' of an array or slice type
                return parse_var_decl();
            }
            // Fall through
        default:
            if (auto expr = parse_expression(); expr) {
                return expr;
            }
            append(t, "Unexpected symbol `{:c}`", static_cast<char>(t.symbol_code()));
            lexer.lex();
            return {};
        }
    case TokenKind::Raw: {
        auto raw = t.raw_text();
        assert(raw.marker == LiaComptimeBlock::begin);
        lexer.lex();
        if (raw.end) {
            return make_node<Comptime>(t.location, text_at(raw.start, *raw.end));
        } else {
            append(t.location, "Unclosed `@comptime` block");
            return {};
        }
    }
    default:
        lexer.lex();
        append(t, L"Unexpected token `{}`", text_of(t));
        return {};
    }
}

std::wstring_view Parser::text_at(size_t start, std::optional<size_t> end) const
{
    if (start < text.length()) {
        if (end) {
            return text.substr(start, *end - start);
        } else {
            return text.substr(start);
        }
    }
    return L"";
}

std::wstring_view Parser::text_of(Token const &token) const
{
    return text_of(token.location);
}

std::wstring_view Parser::text_of(LexerErrorMessage const &error) const
{
    return text_of(error.location);
}

std::wstring_view Parser::text_of(LexerError const &error) const
{
    return text_of(error.error().location);
}

std::wstring_view Parser::text_of(LexerResult const &res) const
{
    if (!res.has_value()) {
        return text_of(res.error().location);
    }
    return text_of(res.value().location);
}

std::wstring_view Parser::text_of(TokenLocation const &location) const
{
    if (location.index < text.length()) {
        return text.substr(location.index, location.length);
    }
    return L"";
}

ASTNode parse_number(Parser &parser, Parser::Token number)
{
    std::wstring_view frac { L"" };
    std::wstring_view exponent { L"" };

    parser.lexer.lex();
    if (parser.lexer.accept_symbol('.')) {
        if (auto frac_maybe { parser.lexer.accept_number() }; frac_maybe) {
            frac = parser.text_of(*frac_maybe);
        }
    }

    auto bm { parser.lexer.bookmark() };
    if (auto e = parser.lexer.accept_identifier(); e) {
        auto e_text { parser.text_of(*e) };
        if (e_text == L"e" || e_text == L"E") {
            if (auto exp { parser.lexer.accept_number() }; exp) {
                exponent = parser.text_of(*exp);
            }
        } else {
            parser.lexer.push_back(bm);
        }
    }

    if (!frac.empty() || !exponent.empty()) {
        return parser.make_node<Decimal>(
            number.location + parser.lexer.last_location,
            parser.text_of(number), frac, exponent);
    }
    auto num { parser.text_of(number) };
    return parser.make_node<Number>(number.location, parser.text_of(number), number.radix());
}

ASTNode Parser::parse_primary()
{
    auto token = lexer.peek();
    // trace("parse_primary() t = {} [{}]", as_utf8(text_of(token)), TokenKind_name(token.kind));
    ASTNode ret { nullptr };
    switch (token.kind) {
    case TokenKind::Number: {
        ret = parse_number(*this, token);
        break;
    }
    case TokenKind::QuotedString: {
        lexer.lex();
        if (token.quoted_string().quote_type == QuoteType::SingleQuote && token.location.length != 1) {
            append(token, "Single quoted string should contain exactly one character");
            return {};
        }
        ret = make_node<QuotedString>(token.location, text_of(token), token.quoted_string().quote_type);
        break;
    }
    case TokenKind::Identifier: {
        lexer.lex();
        // auto bm = lexer.bookmark();
        // if (lexer.accept_symbol('<')) {
        //     TypeSpecifications specs;
        //     while (true) {
        //         auto spec = parse_type();
        //         if (spec == nullptr) {
        //             break;
        //         }
        //         specs.push_back(spec);
        //         if (lexer.accept_symbol('>')) {
        //             return make_node<StampedIdentifier>(token.location + lexer.location(), text_of(token), specs);
        //         }
        //         if (!lexer.accept_symbol(',')) {
        //             break;
        //         }
        //     }
        // }
        // lexer.push_back(bm);
        ret = make_node<Identifier>(token.location, text_of(token));
        break;
    }
    case TokenKind::Keyword:
        if (token.matches_keyword(LiaKeyword::Embed)) {
            return parse_embed();
        }
        if (token.matches_keyword(LiaKeyword::Include)) {
            return parse_include();
        }
        if (token.matches_keyword(LiaKeyword::False)) {
            lexer.lex();
            return make_node<BoolConstant>(token.location, false);
        }
        if (token.matches_keyword(LiaKeyword::True)) {
            lexer.lex();
            return make_node<BoolConstant>(token.location, true);
        }
        if (token.matches_keyword(LiaKeyword::Null)) {
            lexer.lex();
            return make_node<Nullptr>(token.location);
        }
        if (auto const op_maybe = check_prefix_op(); op_maybe) {
            auto &op = *op_maybe;
            auto  bp = binding_power(op);
            auto  op_token = lexer.lex();
            auto  operand = (op.op == Operator::Sizeof) ? parse_type() : parse_expression(bp.right);
            if (!operand) {
                append(token, "Expected operand following prefix operator '{}'", Operator_name(op.op));
                return {};
            }
            ret = make_node<UnaryExpression>(op_token.location + operand->location, op.op, operand);
            break;
        }
        append(token, "Unexpected keyword '{}' parsing primary expression", LiaKeyword_name(token.keyword()));
        return {};
    case TokenKind::Symbol: {
        if (token.symbol_code() == '(') {
            lexer.lex();
            if (lexer.accept_symbol(')')) {
                return make_node<Void>(token.location);
            }
            ret = parse_expression();
            if (auto err = lexer.expect_symbol(')'); !err.has_value()) {
                append(err.error(), "Expected ')'");
                return {};
            }
            break;
        }
        if (token.symbol_code() == '{') {
            lexer.lex();
            if (lexer.accept_symbol('}')) {
                return make_node<Void>(token.location);
            }
            ret = parse_braced_initializer();
            break;
        }
        if (auto const op_maybe = check_prefix_op(); op_maybe) {
            auto &op = *op_maybe;
            auto  bp = binding_power(op);
            auto  op_token = lexer.lex();
            auto  operand = parse_expression(bp.right);
            if (!operand) {
                append(token, "Expected operand following prefix operator '{}'", Operator_name(op.op));
                return {};
            }
            ret = make_node<UnaryExpression>(op_token.location + operand->location, op.op, operand);
            break;
        }
    } // Fall through
    default:
        append(token, L"Unexpected token {} `{}`", as_wstring(TokenKind_name(token.kind)), text_of(token));
        ret = nullptr;
    }
    if (ret == nullptr) {
        append(token, "Expected primary expression");
    }
    return ret;
}

// Shamelessly stolen from here:
// https://matklad.github.io/2020/04/13/simple-but-powerful-pratt-parsing.html
ASTNode Parser::parse_expression(Precedence min_prec)
{
    auto lhs = parse_primary();
    // trace("parse_expression({}) lhs = {}", min_prec, SyntaxNodeType_name(lhs->type));
    if (lhs == nullptr) {
        return {};
    }
    while (!lexer.next_matches(TokenKind::EndOfFile) && check_op()) {
        if (auto op_maybe = check_postfix_op(); op_maybe) {
            auto op = op_maybe.value();
            auto bp = binding_power(op);
            // trace("parse_expression({}) postfix op = {} ({})", min_prec, Operator_name(op.op), bp.left);
            if (bp.left < min_prec) {
                break;
            }
            if (op.op == Operator::Subscript) {
                lexer.lex();
                auto rhs = parse_expression();
                if (rhs == nullptr) {
                    append(lexer.peek().location, "Expected subscript expression");
                    return {};
                }
                if (auto err = lexer.expect_symbol(']'); !err.has_value()) {
                    append(err.error(), "Expected ']'");
                    return {};
                }
                lhs = make_node<BinaryExpression>(lhs->location + rhs->location, lhs, op_maybe->op, rhs);
            } else {
                lhs = make_node<UnaryExpression>(lhs->location + lexer.peek().location, op_maybe->op, lhs);
                lexer.lex();
            }
            continue;
        }
        if (auto op_maybe = check_binop(); op_maybe) {
            auto op = op_maybe.value();
            auto bp = binding_power(op);
            // trace("parse_expression({}) infix op = {} ({})", min_prec, Operator_name(op.op), bp.left);
            if (bp.left < min_prec) {
                break;
            }
            if (op.op == Operator::Call) {
                // Don't lex the '(' so parse_primary will return a
                // single expression, probably a binop with op = ','.
                auto param_list = parse_primary();
                if (param_list == nullptr) {
                    append(lhs->location, "Could not parse function call argument list");
                    return {};
                }
                // trace("parse_expression() param_list = {}", SyntaxNodeType_name(param_list->type));
                lhs = make_node<BinaryExpression>(lhs->location + param_list->location, lhs, Operator::Call, param_list);
            } else {
                auto token = lexer.lex();
                auto rhs = (op.op == Operator::Cast) ? parse_type() : parse_expression(bp.right);
                if (rhs == nullptr) {
                    return {};
                }
                // trace("parse_expression({}) rhs = {}", min_prec, SyntaxNodeType_name(rhs->type));
                lhs = make_node<BinaryExpression>(lhs->location + rhs->location, lhs, op.op, rhs);
            }
            continue;
        }
        break;
    }
    return lhs;
}

bool Parser::check_op()
{
    auto const &token = lexer.peek();
    if (!token.matches(TokenKind::Symbol) && !token.matches(TokenKind::Keyword)) {
        return false;
    }
    auto ret = std::any_of(
        operators.begin(),
        operators.end(),
        [&token](auto const &def) -> bool {
            return std::visit(
                overloads {
                    [&token](wchar_t sym) { return token.matches_symbol(sym); },
                    [&token](LiaKeyword sym) { return token.matches_keyword(sym); } },
                def.sym);
        });
    return ret;
}

std::optional<Parser::OperatorDef> Parser::check_binop()
{
    auto const &token = lexer.peek();
    if (token.kind != TokenKind::Symbol && token.kind != TokenKind::Keyword) {
        return {};
    }
    for (auto const &def : operators) {
        if (def.position != Position::Infix) {
            continue;
        }
        if (!std::visit(
                overloads {
                    [&token](wchar_t sym) { return token.matches_symbol(sym); },
                    [&token](LiaKeyword sym) { return token.matches_keyword(sym); } },
                def.sym)) {
            continue;
        }
        return def;
    }
    return {};
}

std::optional<Parser::OperatorDef> Parser::check_prefix_op()
{
    auto const &token = lexer.peek();
    if (token.kind != TokenKind::Symbol && token.kind != TokenKind::Keyword) {
        return {};
    }
    for (auto const &def : operators) {
        if (def.position != Position::Prefix) {
            continue;
        }
        if (!std::visit(
                overloads {
                    [&token](wchar_t sym) { return token.matches_symbol(sym); },
                    [&token](LiaKeyword sym) { return token.matches_keyword(sym); } },
                def.sym)) {
            continue;
        }
        return def;
    }
    return {};
}

std::optional<Parser::OperatorDef> Parser::check_postfix_op()
{
    auto const &token = lexer.peek();
    if (token.kind != TokenKind::Symbol && token.kind != TokenKind::Keyword) {
        return {};
    }
    for (auto const &def : operators) {
        if (def.position != Position::Postfix) {
            continue;
        }
        if (!std::visit(
                overloads {
                    [&token](wchar_t sym) { return token.matches_symbol(sym); },
                    [&token](LiaKeyword sym) { return token.matches_keyword(sym); } },
                def.sym)) {
            continue;
        }
        return def;
    }
    return {};
}

ASTNode Parser::parse_braced_initializer()
{
    auto ret = parse_expression();
    if (auto err = lexer.expect_symbol('}'); !err.has_value()) {
        append(err.error(), "Expected '}'");
        return {};
    }
    return ret;
}

ASTNode Parser::parse_type()
{
    auto t = lexer.peek();
    if (lexer.accept_symbol('&')) {
        if (auto type = parse_type(); type != nullptr) {
            return make_node<TypeSpecification>(t.location + type->location, ReferenceDescriptionNode { type });
        }
        return {};
    }
    if (lexer.accept_symbol('?')) {
        if (auto type = parse_type(); type != nullptr) {
            return make_node<TypeSpecification>(t.location + type->location, OptionalDescriptionNode { type });
        }
        return {};
    }
    if (lexer.accept_symbol('[')) {
        if (lexer.accept_symbol(']')) {
            if (auto type = parse_type(); type != nullptr) {
                return make_node<TypeSpecification>(t.location + type->location, SliceDescriptionNode { type });
            }
            return {};
        }
        if (lexer.accept_symbol('0')) {
            if (auto err = lexer.expect_symbol(']'); !err.has_value()) {
                append(err.error(), "Expected `]` to close `[0`");
                return {};
            }
            if (auto type = parse_type(); type != nullptr) {
                return make_node<TypeSpecification>(t.location + type->location, ZeroTerminatedArrayDescriptionNode { type });
            }
            return {};
        }
        if (lexer.accept_symbol('*')) {
            if (auto err = lexer.expect_symbol(']'); !err.has_value()) {
                append(err.error(), "Expected `]` to close `[*`");
                return {};
            }
            if (auto type = parse_type(); type != nullptr) {
                return make_node<TypeSpecification>(t.location + type->location, DynArrayDescriptionNode { type });
            }
            return {};
        }
        if (auto res = lexer.expect(TokenKind::Number); !res.has_value()) {
            append(res.error(), "Expected integer array size, `0`, or `]`");
            return {};
        } else {
            if (auto err = lexer.expect_symbol(']'); !err.has_value()) {
                append(err.error(), "Expected `]` to close array descriptor");
                return {};
            }
            auto size = string_to_integer<size_t>(text_of(res.value()));
            assert(size.has_value());
            if (auto type = parse_type(); type != nullptr) {
                return make_node<TypeSpecification>(t.location + type->location, ArrayDescriptionNode { type, size.value() });
            }
            return {};
        }
    }

    Strings       name;
    TokenLocation start = lexer.peek().location;
    while (true) {
        auto n = lexer.accept_identifier();
        if (!n) {
            append(lexer.peek(), "Expected type name");
            name.clear();
            break;
        }
        name.emplace_back(std::wstring { text_of(n.value()) });
        if (!lexer.accept_symbol('.')) {
            break;
        }
    }
    if (name.empty()) {
        return {};
    }
    ASTNodes arguments;
    if (lexer.accept_symbol('<')) {
        while (true) {
            if (lexer.accept_symbol('>')) {
                break;
            }
            auto arg = parse_type();
            if (arg == nullptr) {
                append(lexer.peek(), "Expected template type specification");
                return {};
            }
            arguments.push_back(arg);
            auto t = lexer.peek();
            if (lexer.accept_symbol('>')) {
                break;
            }
            if (auto err = lexer.expect_symbol(','); !err.has_value()) {
                append(err.error(), "Expected `,` or `>`");
                return {};
            }
        }
    }
    auto type = make_node<TypeSpecification>(
        start + lexer.last_location,
        TypeNameNode { std::move(name), std::move(arguments) });
    if (lexer.accept_symbol('/')) {
        if (auto error_type = parse_type(); error_type != nullptr) {
            return make_node<TypeSpecification>(
                start + lexer.last_location,
                ResultDescriptionNode { type, error_type });
        }
        return {};
    }
    return type;
}

ASTNode Parser::parse_break_continue()
{
    auto kw = lexer.lex();
    assert(kw.matches_keyword(LiaKeyword::Break) || kw.matches_keyword(LiaKeyword::Continue));
    Label label {};
    if (lexer.accept_symbol(':')) {
        auto lbl = lexer.peek();
        if (!lbl.matches(TokenKind::Identifier)) {
            append(lbl, "Expected label name after `:`");
            return {};
        }
        label = text_of(lbl);
    }
    if (kw.matches_keyword(LiaKeyword::Break)) {
        return make_node<Break>(kw.location, label);
    }
    return make_node<Continue>(kw.location, label);
}

ASTNode Parser::parse_embed()
{
    auto kw = lexer.lex();
    auto token = lexer.peek();
    if (auto res = lexer.expect_symbol('('); !res.has_value()) {
        append(res.error());
        return {};
    }
    auto file_name = lexer.expect(TokenKind::QuotedString);
    if (!file_name.has_value()) {
        append(file_name.error());
        return {};
    }
    auto fname = text_of(file_name.value());
    fname = fname.substr(0, fname.length() - 1).substr(1);
    if (auto res = lexer.expect_symbol(')'); !res.has_value()) {
        append(lexer.location(), "Expected `)`");
        return {};
    }
    return make_node<Embed>(kw.location + lexer.location(), fname);
}

ASTNode Parser::parse_defer()
{
    auto kw = lexer.lex();
    if (auto stmt = parse_statement(); stmt == nullptr) {
        append(kw, "Could not parse defer statement");
        return {};
    } else {
        return make_node<DeferStatement>(kw.location + stmt->location, stmt);
    }
}

ASTNode Parser::parse_enum()
{
    auto enum_token = lexer.lex();
    assert(enum_token.matches_keyword(LiaKeyword::Enum));

    auto name = lexer.expect_identifier();
    if (!name.has_value()) {
        append(lexer.last_location, "Expected enum name");
        return {};
    }
    ASTNode underlying { nullptr };
    if (lexer.accept_symbol(':')) {
        if (underlying = parse_type(); underlying == nullptr) {
            append(lexer.last_location, "Expected underlying type after `:`");
            return {};
        }
    }
    if (auto res = lexer.expect_symbol('{'); !res.has_value()) {
        append(res.error().location, res.error().message);
        return {};
    }
    ASTNodes values;
    while (!lexer.accept_symbol('}')) {
        auto label = lexer.expect_identifier();
        if (!label.has_value()) {
            append(label.error().location, label.error().message);
            return {};
        }
        ASTNode payload { nullptr };
        if (lexer.accept_symbol(':')) {
            payload = parse_type();
            if (payload == nullptr) {
                append(lexer.last_location, "Expected enum value payload type");
                return {};
            }
        }
        ASTNode value_node { nullptr };
        if (lexer.accept_symbol('=')) {
            auto value = lexer.peek();
            if (!value.matches(TokenKind::Number)) {
                append(value.location, "Expected enum value"); // Make better
                return {};
            }
            lexer.lex();
            value_node = make_node<Number>(value.location, text_of(value), value.radix());
        }
        values.emplace_back(make_node<EnumValue>(
            label.value().location + lexer.last_location,
            std::wstring { text_of(label) },
            value_node,
            payload));
        if (!lexer.accept_symbol(',') && !lexer.next_matches('}')) {
            append(lexer.last_location, "Expected `,` or `}`");
            return {};
        }
    }
    return make_node<Enum>(
        enum_token.location + lexer.last_location,
        std::wstring { text_of(name) },
        underlying,
        values);
}

ASTNode Parser::parse_for()
{
    Label         label;
    TokenLocation location;
    if (lexer.has_lookback(1)
        && lexer.lookback(0).matches_symbol(':')
        && lexer.lookback(1).matches(TokenKind::Identifier)) {
        label = text_of(lexer.lookback(1));
        location = lexer.lookback(1).location;
    }
    auto for_token = lexer.lex();
    assert(for_token.matches_keyword(LiaKeyword::For));
    if (!label.has_value()) {
        location = for_token.location;
    }

    auto var_name = lexer.peek();
    if (auto res = lexer.expect_identifier(); !res.has_value()) {
        append(res.error(), "Expected `for` range variable name");
        return {};
    }
    auto token = lexer.peek();
    if (token.matches(TokenKind::Identifier) && text_of(token) == L"in") {
        lexer.lex();
    }
    token = lexer.peek();
    auto range = parse_expression();
    if (range == nullptr) {
        append(token, "Error parsing `for` range");
        return {};
    }
    token = lexer.peek();
    auto stmt = parse_statement();
    if (stmt == nullptr) {
        append(token, "Error parsing `for` block");
        return {};
    }
    return make_node<ForStatement>(location + stmt->location, std::wstring { text_of(var_name) }, range, stmt);
}

ASTNode Parser::parse_func()
{
    auto func = lexer.lex();
    auto old_level = level;
    level = ParseLevel::Function;
    std::wstring name;
    if (auto res = lexer.expect_identifier(); !res.has_value()) {
        append(res.error(), "Expected function name");
        level = old_level;
        return {};
    } else {
        name = text_of(res.value());
    }

    ASTNodes generics;
    if (lexer.accept_symbol('<')) {
        while (true) {
            if (lexer.accept_symbol('>')) {
                break;
            }
            std::wstring  generic_name;
            TokenLocation start;
            if (auto res = lexer.expect_identifier(); !res.has_value()) {
                append(res.error(), "Expected generic name");
                level = old_level;
                return {};
            } else {
                generics.emplace_back(make_node<Identifier>(res.value().location, text_of(res.value())));
            }
            if (lexer.accept_symbol('>')) {
                break;
            }
            if (auto res = lexer.expect_symbol(','); !res.has_value()) {
                append(res.error(), "Expected ',' in function signature generic list");
            }
        }
    }

    if (auto res = lexer.expect_symbol('('); !res.has_value()) {
        append(res.error(), "Expected '(' in function definition");
    }
    ASTNodes params;
    while (true) {
        if (lexer.accept_symbol(')')) {
            break;
        }
        std::wstring  param_name;
        TokenLocation start;
        if (auto res = lexer.expect_identifier(); !res.has_value()) {
            append(res.error(), "Expected parameter name");
            level = old_level;
            return {};
        } else {
            param_name = text_of(res.value());
            start = res.value().location;
        }
        if (auto res = lexer.expect_symbol(':'); !res.has_value()) {
            append(res.error(), "Expected ':' in function parameter declaration");
        }
        auto          param_type = parse_type();
        TokenLocation end;
        if (param_type == nullptr) {
            append(lexer.peek(), "Expected parameter type");
            level = old_level;
            return {};
        }
        params.emplace_back(make_node<Parameter>(start + param_type->location, param_name, param_type));
        if (lexer.accept_symbol(')')) {
            break;
        }
        if (auto res = lexer.expect_symbol(','); !res.has_value()) {
            append(res.error(), "Expected ',' in function signature");
        }
    }
    auto          return_type = parse_type();
    TokenLocation return_type_loc;
    if (return_type == nullptr) {
        append(lexer.peek(), "Expected return type");
        level = old_level;
        return {};
    }
    auto decl = make_node<FunctionDeclaration>(
        func.location + return_type->location,
        name,
        generics,
        params,
        return_type);
    if (lexer.accept_keyword(LiaKeyword::ExternLink)) {
        if (auto res = lexer.expect(TokenKind::QuotedString); !res.has_value() || res.value().quoted_string().quote_type != QuoteType::DoubleQuote) {
            append(res.error(), "Expected extern function name");
            level = old_level;
            return {};
        } else {
            auto name = text_of(res.value());
            if (name.length() <= 2) {
                append(res.value(), "Invalid extern function name");
            }
            name = name.substr(0, name.size() - 1).substr(1);
            level = old_level;
            return make_node<FunctionDefinition>(
                decl->location + res.value().location,
                get<FunctionDeclaration>(decl).name,
                decl,
                make_node<ExternLink>(res.value().location, std::wstring { name }));
        }
    }
    if (auto impl = parse_statement(); impl != nullptr) {
        level = old_level;
        return make_node<FunctionDefinition>(
            decl->location + impl->location,
            get<FunctionDeclaration>(decl).name,
            decl,
            impl);
    }
    level = old_level;
    return {};
}

ASTNode Parser::parse_if()
{
    auto if_token = lexer.lex();
    assert(if_token.matches_keyword(LiaKeyword::If));
    auto condition = parse_expression();
    if (condition == nullptr) {
        append(if_token, "Error parsing `if` condition");
        return {};
    }
    auto if_branch = parse_statement();
    if (if_branch == nullptr) {
        append(if_token, "Error parsing `if` branch");
        return {};
    }
    ASTNode else_branch { nullptr };
    auto    else_kw = lexer.peek();
    if (lexer.accept_keyword(LiaKeyword::Else)) {
        else_branch = parse_statement();
        if (else_branch == nullptr) {
            append(else_kw, "Error parsing `else` branch");
            return {};
        }
    }
    return make_node<IfStatement>(
        if_token.location + (else_branch != nullptr ? else_branch->location : if_branch->location),
        condition, if_branch, else_branch);
}

ASTNode Parser::parse_import()
{
    auto import_token = lexer.lex();
    assert(import_token.matches_keyword(LiaKeyword::Import));
    Strings       path;
    TokenLocation end_location = import_token.location;
    do {
        auto ident_maybe = lexer.expect_identifier();
        if (!ident_maybe.has_value()) {
            append(ident_maybe.error(), "Expected import path component");
            return {};
        }
        path.emplace_back(text_of(ident_maybe));
        end_location = ident_maybe.value().location;
        if (!lexer.accept_symbol('.')) {
            break;
        }
    } while (true);
    return make_node<Import>(import_token.location + end_location, path);
}

ASTNode Parser::parse_include()
{
    auto kw = lexer.lex();
    if (auto res = lexer.expect_symbol('('); !res.has_value()) {
        append(res.error(), "Malformed '@include' statement: expected '('");
        return {};
    }
    auto file_name = lexer.expect(TokenKind::QuotedString);
    if (!file_name.has_value()) {
        append(file_name.error(), "Malformed '@include' statement: no file name");
        return {};
    }
    auto fname = text_of(file_name.value());
    fname = fname.substr(0, fname.length() - 1).substr(1);
    auto close_paren = lexer.peek();
    if (auto res = lexer.expect_symbol(')'); !res.has_value()) {
        append(res.error(), L"Malformed '@include' statement: expected ')', got '{}'", text_of(res.error().location));
        return {};
    }
    return make_node<Include>(kw.location + close_paren.location, fname);
}

ASTNode Parser::parse_loop()
{
    Label         label;
    TokenLocation location;
    if (lexer.has_lookback(1)
        && lexer.lookback(0).matches_symbol(':')
        && lexer.lookback(1).matches(TokenKind::Identifier)) {
        label = text_of(lexer.lookback(1));
        location = lexer.lookback(1).location;
    }
    auto loop_token = lexer.lex();
    assert(loop_token.matches_keyword(LiaKeyword::Loop));
    if (!label.has_value()) {
        location = loop_token.location;
    }
    auto stmt = parse_statement();
    if (stmt == nullptr) {
        append(loop_token, "Error parsing `loop` block");
        return {};
    }
    auto ret = make_node<LoopStatement>(location + stmt->location, label, stmt);
    return ret;
}

ASTNode Parser::parse_public()
{
    auto t = lexer.peek();
    assert(t.matches_keyword(LiaKeyword::Public));
    lexer.lex();
    auto decl = parse_module_level_statement();
    if (decl == nullptr) {
        return {};
    }
    std::optional<std::wstring> name = std::visit(
        overloads {
            [](Enum const &n) -> std::optional<std::wstring> {
                return n.name;
            },
            [](FunctionDefinition const &n) -> std::optional<std::wstring> {
                return n.name;
            },
            [this, &decl](PublicDeclaration const &n) -> std::optional<std::wstring> {
                append(decl->location, L"Double public declaration");
                return {};
            },
            [](Struct const &n) -> std::optional<std::wstring> {
                return n.name;
            },
            [](VariableDeclaration const &n) -> std::optional<std::wstring> {
                return n.name;
            },
            [this, &decl](auto const &) -> std::optional<std::wstring> {
                append(decl->location, "Cannot declare statement of type `{}` public", SyntaxNodeType_name(decl->type()));
                return {};
            } },
        decl->node);
    if (!name) {
        return {};
    }
    return make_node<PublicDeclaration>(t.location + decl->location, *name, decl);
}

ASTNode Parser::parse_return()
{
    auto kw = lexer.lex();
    assert(kw.matches_keyword(LiaKeyword::Return));
    auto expr = parse_expression();
    if (!expr) {
        append(kw.location, "Error parsing return expression");
        return {};
    }
    return make_node<Return>(kw.location + expr->location, expr);
}

ASTNode Parser::parse_struct()
{
    auto struct_token = lexer.lex();
    assert(struct_token.matches_keyword(LiaKeyword::Struct));

    auto name = lexer.expect_identifier();
    if (!name.has_value()) {
        append(lexer.last_location, "Expected struct name");
        return {};
    }
    if (auto res = lexer.expect_symbol('{'); !res.has_value()) {
        append(res.error().location, res.error().message);
        return {};
    }
    ASTNodes members;
    while (!lexer.accept_symbol('}')) {
        auto label = lexer.expect_identifier();
        if (!label.has_value()) {
            append(label.error().location, label.error().message);
            return {};
        }
        if (auto err = lexer.expect_symbol(':'); !err.has_value()) {
            append(err.error().location, "Expected `:`");
            return {};
        }
        auto type = parse_type();
        if (type == nullptr) {
            append(lexer.last_location, "Expected struct member type");
            return {};
        }
        members.emplace_back(make_node<StructMember>(
            label.value().location + lexer.last_location,
            std::wstring { text_of(label) },
            type));
        if (!lexer.accept_symbol(',') && !lexer.next_matches('}')) {
            append(lexer.last_location, "Expected `,` or `}`");
            return {};
        }
    }
    return make_node<Struct>(
        struct_token.location + lexer.last_location,
        std::wstring { text_of(name) },
        members);
}

ASTNode Parser::parse_var_decl()
{
    assert(lexer.has_lookback(1)
        && lexer.lookback(0).matches_symbol(':')
        && lexer.lookback(1).matches(TokenKind::Identifier));
    bool    is_const = lexer.has_lookback(2) && lexer.lookback(2).matches_keyword(LiaKeyword::Const);
    auto    name = lexer.lookback(1);
    Token   token = lexer.peek();
    ASTNode type_name {};
    auto    location = lexer.lookback(is_const ? 2 : 1).location;
    auto    end_location = token.location;
    if (!token.matches_symbol('=')) {
        type_name = parse_type();
        if (type_name == nullptr) {
            append(lexer.peek(), "Expected variable type specification");
            return {};
        }
        end_location = type_name->location;
    }
    token = lexer.peek();
    ASTNode initializer = nullptr;
    if (token.matches_symbol('=')) {
        lexer.lex();
        initializer = parse_expression();
        if (initializer == nullptr) {
            append(token.location, "Error parsing initialization expression");
            return {};
        }
        end_location = initializer->location;
    } else if (!type_name) {
        append(token, "Expected variable initialization expression");
        return {};
    } else {
        end_location = token.location;
    }
    auto ret = make_node<VariableDeclaration>(
        location + end_location,
        std::wstring { text_of(name) },
        type_name,
        initializer,
        is_const);
    return ret;
}

ASTNode Parser::parse_while()
{
    Label         label;
    TokenLocation location;
    if (lexer.has_lookback(1)
        && lexer.lookback(0).matches_symbol(':')
        && lexer.lookback(1).matches(TokenKind::Identifier)) {
        label = text_of(lexer.lookback(1));
        location = lexer.lookback(1).location;
    }
    auto while_token = lexer.lex();
    assert(while_token.matches_keyword(LiaKeyword::While));
    if (!label.has_value()) {
        location = while_token.location;
    }
    auto condition = parse_expression();
    if (condition == nullptr) {
        append(while_token, "Error parsing `while` condition");
        return {};
    }
    auto stmt = parse_statement();
    if (stmt == nullptr) {
        append(while_token, "Error parsing `while` block");
        return {};
    }
    auto ret = make_node<WhileStatement>(location + stmt->location, label, condition, stmt);
    return ret;
}

ASTNode Parser::parse_yield()
{
    auto kw = lexer.lex();
    assert(kw.matches_keyword(LiaKeyword::Yield));
    Label label {};
    if (lexer.accept_symbol(':')) {
        if (auto res = lexer.expect_identifier(); !res.has_value()) {
            append(res.value(), "Expected label name after `:`");
            return {};
        } else {
            label = text_of(res.value());
        }
    }
    if (auto stmt = parse_statement(); stmt == nullptr) {
        append(kw, "Could not parse yield expression");
        return {};
    } else {
        return make_node<Yield>(kw.location, label, stmt);
    }
}

BindResult Parser::bind(ASTNode node)
{
    auto node_was_program { false };
    if (node == nullptr) {
        node = program;
    }
    assert(node != nullptr);
    pass = 0;
    unbound = std::numeric_limits<int>::max();
    int        prev_pass;
    BindResult s;
    do {
        prev_pass = unbound;
        unbound = 0;
        unbound_nodes.clear();
        s = Lia::bind(node);
        ++pass;
    } while (!s.has_value() && unbound < prev_pass);
    return s;
}

// #define DEBUG_NAMESPACE_STACK

void dump_namespace_stack(Parser const &parser, auto const &prefix)
{
#ifdef DEBUG_NAMESPACE_STACK
    std::stringstream ss;
    ss << prefix;
    std::ranges::for_each(
        parser.namespaces | std::ranges::views::reverse,
        [&ss](auto const &n) {
            ss << " <- " << n->node.value() << ' ' << SyntaxNodeType_name(n->node->type()) << ' ' << n.id.value();
        });
    info("{}", ss.str());
#endif
}

template<typename Ret, typename Fun>
Ret find_in_node(Parser const &parser, Strings const &name, Fun const &function)
{
    assert(!parser.namespaces.empty());
    dump_namespace_stack(parser, std::format("[S* {}]", as_utf8(name.back())));
    auto ns { parser.namespaces.back() };
    for (auto const &n : name | std::ranges::views::take(name.size() - 1)) {
        auto mod { ns->find_module(n) };
        if (mod == nullptr) {
            return Ret {};
        }
        ns = mod->ns;
        if (ns == nullptr) {
            return Ret {};
        }
    }
    return function(ns, name.back());
}

bool Parser::has_function(Strings const &name, pType const &type) const
{
    return find_in_node<bool>(
        *this,
        name,
        [&type](NSNode const &ns, std::wstring const &n) -> bool {
            return ns->find_function(n, type) != nullptr;
        });
}

ASTNode Parser::find_function(Strings const &name, pType const &type) const
{
    return find_in_node<ASTNode>(
        *this,
        name,
        [&type](NSNode const &ns, std::wstring const &n) -> ASTNode {
            return ns->find_function(n, type);
        });
}

// ASTNode Parser::find_function_by_arg_list(std::wstring const &name, pType const &type) const
//{
//     assert(is<TypeList>(type));
//     assert(!namespaces.empty());
//     return namespaces.back()->find_function_by_arg_list(name, type);
// }

pType Parser::type_of(Strings const &name) const
{
    return find_in_node<pType>(
        *this,
        name,
        [](NSNode const &ns, std::wstring const &n) -> pType {
            return ns->type_of(n);
        });
}

ASTNodes Parser::find_overloads(Strings const &names, ASTNodes const &type_args) const
{
    return find_in_node<ASTNodes>(
        *this,
        names,
        [&type_args](NSNode const &ns, std::wstring const &n) -> ASTNodes {
            return ns->find_overloads(n, type_args);
        });
}

void Parser::register_variable(std::wstring name, ASTNode node)
{
    assert(!namespaces.empty());
    namespaces.back()->register_variable(std::move(name), std::move(node));
}

bool Parser::has_variable(Strings const &name) const
{
    return find_in_node<bool>(
        *this,
        name,
        [](NSNode const &ns, std::wstring const &n) -> bool {
            return ns->has_variable(n);
        });
}

void Parser::register_function(std::wstring name, ASTNode fnc)
{
    assert(!namespaces.empty());
    namespaces.back()->register_function(std::move(name), std::move(fnc));
}

pType Parser::find_type(Strings const &name) const
{
    return find_in_node<pType>(
        *this,
        name,
        [](NSNode const &ns, std::wstring const &n) -> pType {
            return ns->find_type(n);
        });
}

ASTNode Parser::current_function() const
{
    assert(!namespaces.empty());
    return namespaces.back()->current_function();
}

void Parser::register_type(std::wstring name, pType type)
{
    assert(!namespaces.empty());
    namespaces.back()->register_type(std::move(name), std::move(type));
}

void Parser::clear_namespaces()
{
    namespaces.clear();
}

void Parser::push_namespace(ASTNode const &n)
{
    assert(n->ns);
    if (!namespaces.empty() && n->ns->parent == nullptr) {
        n->ns->parent = namespaces.back();
    }
    namespaces.push_back(n->ns);
    dump_namespace_stack(*this, std::format("[S+ {}]", n.value()));
}

void Parser::pop_namespace(ASTNode const &n)
{
    assert(!namespaces.empty());
    if (namespaces.back()->node.value() == n.value()) {
        namespaces.pop_back();
        dump_namespace_stack(*this, std::format("[S- {}]", n.value()));
    }
}

void Parser::append(LexerErrorMessage const &lexer_error)
{
    append(lexer_error.location, MUST_EVAL(to_wstring(lexer_error.message)));
}

void Parser::append(LexerErrorMessage const &lexer_error, char const *message)
{
    append(lexer_error.location, message);
}

void Parser::append(LexerErrorMessage const &lexer_error, wchar_t const *message)
{
    append(lexer_error.location, message);
}

void Parser::append(Token const &token, wchar_t const *message)
{
    append(token.location, message);
}

void Parser::append(Token const &token, char const *message)
{
    append(token.location, message);
}

void Parser::append(TokenLocation location, std::wstring message)
{
    errors.emplace_back(std::move(location), std::move(message));
}

void Parser::append(TokenLocation location, std::string const &message)
{
    append(std::move(location), MUST_EVAL(to_wstring(message)));
}

void Parser::append(TokenLocation location, wchar_t const *message)
{
    append(std::move(location), std::wstring { message });
}

void Parser::append(TokenLocation location, char const *message)
{
    append(std::move(location), MUST_EVAL(to_wstring(message)));
}

BindError Parser::bind_error(LiaError const &error)
{
    errors.emplace_back(error);
    return BindError { ASTStatus::BindErrors };
}

BindError Parser::bind_error(TokenLocation location, std::wstring const &msg)
{
    append(location, msg);
    return BindError { ASTStatus::BindErrors };
}

BindError Parser::bind_error(TokenLocation location, std::string const &msg)
{
    append(location, msg);
    return BindError { ASTStatus::BindErrors };
}

ASTNode add_node(ASTNode from, SyntaxNode impl)
{
    ASTNode ret = from.repo->add_node(from->location, std::move(impl));
    if (from->ns != nullptr) {
        ret->ns = from->ns;
    }
    ret->supercedes = from;
    from->superceded_by = ret;
    return ret;
}
}
