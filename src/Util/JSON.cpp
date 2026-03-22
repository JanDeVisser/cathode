/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/IO.h>
#include <Util/JSON.h>
#include <Util/Lexer.h>

namespace Util {

using namespace std::literals;

[[nodiscard]] std::string JSONValue::to_string() const
{
    switch (type()) {
    case JSONType::Null:
        return "null";
    case JSONType::String:
        return std::get<std::string>(m_value);
    case JSONType::Integer:
        return std::to_string(std::get<int64_t>(m_value));
    case JSONType::Boolean:
        return std::get<bool>(m_value) ? "true" : "false";
    case JSONType::Double:
        return std::to_string(std::get<double>(m_value));
    case JSONType::Array: {
        auto const &array = std::get<Array>(m_value);
        if (array.empty())
            return "[]";
        std::string ret = "[ ";
        bool        first = true;
        for (auto const &v : array) {
            if (!first)
                ret += ", ";
            first = false;
            ret += v.serialize();
        }
        return ret + " ]";
    }
    case JSONType::Object: {
        auto const &object = std::get<Object>(m_value);
        if (object.empty())
            return "{}";
        std::string ret = "{ ";
        bool        first = true;
        for (auto const &v : object) {
            if (!first)
                ret += ", ";
            first = false;
            ret += "\"" + v.first + "\": " + v.second.serialize();
        }
        return ret + " }";
    }
    }
    UNREACHABLE();
}

[[nodiscard]] std::string JSONValue::serialize(bool pretty, int indent_width, int indent) const
{
    switch (type()) {
    case JSONType::Null:
    case JSONType::Integer:
    case JSONType::Double:
    case JSONType::Boolean:
        return to_string();
    case JSONType::String: {
        auto s = to_string();
        replace_all(s, "\r", R"(\r)");
        replace_all(s, "\n", R"(\n)");
        replace_all(s, "\t", R"(\t)");
        replace_all(s, "\"", R"(\")");
        return "\"" + s + "\"";
    }
    case JSONType::Array: {
        auto const &array = std::get<Array>(m_value);
        if (array.empty())
            return "[]";
        std::string ret = "[";
        bool        first = true;
        for (auto const &v : array) {
            if (!first)
                ret += ",";
            if (pretty) {
                ret += "\n";
                for (auto i = 0u; i < indent + indent_width; ++i)
                    ret += ' ';
            } else {
                ret += ' ';
            }
            first = false;
            ret += v.serialize(pretty, indent_width, indent + indent_width);
        }
        if (pretty) {
            ret += "\n";
            for (auto i = 0u; i < indent; ++i)
                ret += ' ';
        } else {
            ret += ' ';
        }
        return ret + "]";
    }
    case JSONType::Object: {
        auto const &object = std::get<Object>(m_value);
        if (object.empty())
            return "{}";
        std::string ret = "{";
        bool        first = true;
        for (auto const &v : object) {
            if (!first)
                ret += ",";
            first = false;
            if (pretty) {
                ret += "\n";
                for (auto i = 0u; i < indent + indent_width; ++i)
                    ret += ' ';
            } else {
                ret += ' ';
            }
            ret += "\"" + v.first + "\": " + v.second.serialize();
        }
        if (pretty) {
            ret += "\n";
            for (auto i = 0u; i < indent; ++i)
                ret += ' ';
        } else {
            ret += ' ';
        }
        return ret + "}";
    }
    }
    UNREACHABLE();
}

#define JSONKEYWORD(S) \
    S(True, "true")    \
    S(False, "false")  \
    S(Null, "null")

enum class JSONKeyword {
#undef S
#define S(kw, str) kw,
    JSONKEYWORD(S)
#undef S
};

std::expected<JSONValue, JSONValue::ReadError> JSONValue::read_file(std::string_view const &file_name)
{
    auto json_text_maybe = read_file_by_name(file_name);
    if (!json_text_maybe.has_value()) {
        log_error("Error reading JSON file '{}': {}", file_name, json_text_maybe.error().to_string());
        return std::unexpected(ReadError { json_text_maybe.error() });
    }
    auto json_maybe = JSONValue::deserialize(json_text_maybe.value());
    if (!json_maybe.has_value()) {
        log_error("Error parsing JSON file '{}': {}", file_name, json_maybe.error().to_string());
        return std::unexpected(ReadError { json_maybe.error() });
    }
    return json_maybe.value();
}

using JSONLexerTypes = LexerTypes<std::string_view, char, JSONKeyword>;
using JSONLexer = Lexer<JSONLexerTypes, JSONLexerTypes::CScannerPack>;
using JSONToken = JSONLexerTypes::Token;

std::expected<JSONValue, JSONError> decode_value(JSONLexer &lexer, std::string_view const &str)
{
    auto decode_string = [str](JSONToken const &token) -> std::expected<std::string, JSONError> {
        if (token != TokenKind::QuotedString) {
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                "Expected quoted string",
                static_cast<int>(token.location.line),
                static_cast<int>(token.location.column),
            });
        }
        if (!token.quoted_string().terminated) {
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                "Unterminated string",
                static_cast<int>(token.location.line),
                static_cast<int>(token.location.column),
            });
        }
        if (token.location.length > 2) {
            std::string qstr { str.substr(token.location.index + 1, token.location.length - 2) };
            replace_all(qstr, R"(\r)", "\r"sv);
            replace_all(qstr, R"(\n)", "\n"sv);
            replace_all(qstr, R"(\t)", "\t"sv);
            replace_all(qstr, R"(\")", R"(")");
            replace_all(qstr, R"(\')", "'"sv);
            return { qstr };
        }
        return std::string {};
    };

    auto expect_symbol = [&lexer](int sym) -> std::expected<void, JSONError> {
        auto err = lexer.expect_symbol(sym);
        if (!err.has_value()) {
            auto const &error_token = lexer.peek();
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                std::format("Expected '{:c}'", sym),
                static_cast<int>(error_token.location.line),
                static_cast<int>(error_token.location.column),
            });
        }
        return {};
    };

    auto const &token = lexer.lex();
    switch (token.kind) {
    case TokenKind::Symbol: {
        switch (token.symbol_code()) {
        case '{': {
            auto result = JSONValue::object();
            while (!lexer.accept_symbol('}')) {
                auto const name_token = lexer.lex();
                auto       name = TRY_EVAL(decode_string(name_token));
                TRY(expect_symbol(':'));
                auto value = TRY_EVAL(decode_value(lexer, str));
                result.set(name, value);
                if (!lexer.accept_symbol(',')) {
                    TRY(expect_symbol('}'));
                    break;
                }
            }
            return result;
        }
        case '[': {
            auto result = JSONValue::array();
            while (!lexer.accept_symbol(']')) {
                auto value = TRY_EVAL(decode_value(lexer, str));
                result.append(value);
                if (!lexer.accept_symbol(',')) {
                    TRY(expect_symbol(']'));
                    break;
                }
            }
            return result;
        }
        default:
            return std::unexpected(JSONError {
                JSONError::Code::SyntaxError,
                std::format("Unexpected symbol '{:c}'", token.symbol_code()),
                static_cast<int>(token.location.line),
                static_cast<int>(token.location.column),
            });
        }
    }
    case TokenKind::QuotedString: {
        return JSONValue { TRY_EVAL(decode_string(token)) };
    }
    case TokenKind::Number: {
        if (lexer.accept_symbol('.')) {
            std::string dbl_text { str.substr(token.location.index, token.location.length) };
            if (auto frac { lexer.accept_number() }; frac) {
                dbl_text += ".";
                dbl_text += str.substr((*frac).location.index, (*frac).location.length);
            }
            auto dbl_maybe = string_to_double(dbl_text);
            assert(dbl_maybe.has_value());
            return JSONValue(dbl_maybe.value());
        }
        auto int_maybe = string_to_integer<long>(str.substr(token.location.index, token.location.length));
        assert(int_maybe.has_value());
        return JSONValue(int_maybe.value());
    }
    case TokenKind::Keyword: {
        switch (token.keyword()) {
        case JSONKeyword::False:
            return JSONValue(false);
        case JSONKeyword::True:
            return JSONValue(true);
        case JSONKeyword::Null:
            return JSONValue();
        default:
            UNREACHABLE();
        }
    }
    default:
        return std::unexpected(JSONError {
            JSONError::Code::SyntaxError,
            std::format("Invalid token '{:}' ({:})", str.substr(token.location.index, token.location.length), TokenKind_name(token.kind)),
            static_cast<int>(token.location.line),
            static_cast<int>(token.location.column),
        });
    }
}

using JSONKeywordMatch = KeywordMatch<JSONKeyword>;

template<>
[[nodiscard]] std::optional<JSONKeywordMatch> match_keyword(std::string const &str)
{
#undef S
#define S(KW, STR)                                                                                           \
    if (std::string_view(STR).starts_with(str)) {                                                            \
        return JSONKeywordMatch {                                                                            \
            JSONKeyword::KW,                                                                                 \
            (str == STR) ? JSONKeywordMatch::MatchType::FullMatch : JSONKeywordMatch::MatchType::PrefixMatch \
        };                                                                                                   \
    }
    JSONKEYWORD(S)
#undef S
    return {};
}

std::expected<JSONValue, JSONError> JSONValue::deserialize(std::string_view const &str)
{
    JSONLexer lexer;
    lexer.push_source(str);
    return decode_value(lexer, str);
}

}
