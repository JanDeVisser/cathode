/*
 * Copyright (c) 2023, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cctype>
#include <cwchar>
#include <deque>
#include <format>
#include <string>
#include <string_view>
#include <variant>

#include <Util/Logging.h>
#include <Util/StringUtil.h>
#include <Util/TokenLocation.h>
#include <Util/Utf8.h>

namespace Util {

struct NoSuchEnumValue {
    std::string enum_name;
    std::string value;
};

template<typename ResultType>
using EnumResult = std::expected<ResultType, NoSuchEnumValue>;

#define VALUE_TOKENKINDS(S) \
    S(Symbol)               \
    S(Number)               \
    S(QuotedString)         \
    S(Comment)              \
    S(Raw)

#define TOKENKINDS(S)   \
    S(Unknown)          \
    VALUE_TOKENKINDS(S) \
    S(Keyword)          \
    S(EndOfFile)        \
    S(EndOfLine)        \
    S(Identifier)       \
    S(Tab)              \
    S(Whitespace)       \
    S(Program)          \
    S(Module)

enum class TokenKind {
#undef S
#define S(kind) kind,
    TOKENKINDS(S)
#undef S
};

inline std::string TokenKind_name(TokenKind kind)
{
    switch (kind) {
#undef S
#define S(K)           \
    case TokenKind::K: \
        return std::format("{} *{}*", #K, static_cast<int>(kind));
        TOKENKINDS(S)
#undef S
    default:
        UNREACHABLE();
    }
}

inline EnumResult<TokenKind> TokenKind_from_string(std::string_view const &kind)
{
#undef S
#define S(K)        \
    if (kind == #K) \
        return TokenKind::K;
    TOKENKINDS(S)
#undef S
    return std::unexpected<NoSuchEnumValue>({ "TokenKind", std::string(kind) });
}

#define QUOTETYPES(S)    \
    S(SingleQuote, '\'') \
    S(DoubleQuote, '"')  \
    S(BackQuote, '`')

enum class QuoteType : char {
#undef S
#define S(T, Q) T = (Q),
    QUOTETYPES(S)
#undef S
};

extern std::string           QuoteType_name(QuoteType quote);
extern EnumResult<QuoteType> QuoteType_from_string(std::string_view quote);

#define COMMENTTYPES(S) \
    S(Block)            \
    S(Line)

enum class CommentType {
#undef S
#define S(T) T,
    COMMENTTYPES(S)
#undef S
};

extern std::string             CommentType_name(CommentType quote);
extern EnumResult<CommentType> CommentType_from_string(std::string_view comment);

#define RADIXES(S) \
    S(Decimal, 10) \
    S(Hex, 16)     \
    S(Binary, 2)

enum class Radix : int {
#undef S
#define S(T, R) T = R,
    RADIXES(S)
#undef S
};

struct QuotedString {
    QuoteType quote_type;
    bool      triple;
    bool      terminated;
};

struct CommentText {
    CommentType comment_type;
    bool        terminated;
};

template<typename Char>
struct RawText {
    Char const           *marker;
    size_t                start;
    std::optional<size_t> end;
};

enum class NoKeywordCode {
};

struct LexerErrorMessage {
    TokenLocation location;
    std::string   message;

    std::string const &to_string() const
    {
        return message;
    }
};

template<typename Keyword = NoKeywordCode>
struct KeywordMatch {
    enum class MatchType {
        PrefixMatch,
        FullMatch,
    };
    Keyword   keyword;
    MatchType match_type;
};

template<typename Keyword>
std::optional<KeywordMatch<Keyword>> match_keyword(std::string const &str)
{
    return {};
}

template<typename B, typename C = wchar_t, typename KW = NoKeywordCode>
struct LexerTypes {
    using Buffer = B;
    using Char = C;
    using Keyword = KW;

    using KWMatch = KeywordMatch<Keyword>;

    struct Token {
        using TokenValue = std::variant<std::monostate, Radix, QuotedString, CommentText, RawText<Char>, Char, Keyword>;

        Token() = default;
        Token(Token const &) = default;

        TokenKind     kind { TokenKind::Unknown };
        TokenLocation location {};
        TokenValue    value;

        static Token number(Radix radix)
        {
            Token ret;
            ret.kind = TokenKind::Number;
            ret.value = radix;
            return ret;
        }

        static Token symbol(Char sym)
        {
            Token ret;
            ret.kind = TokenKind::Symbol;
            ret.value.template emplace<Char>(sym);
            return ret;
        }

        static Token keyword(Keyword kw)
        {
            Token ret;
            ret.kind = TokenKind::Keyword;
            ret.value.template emplace<Keyword>(kw);
            return ret;
        }

        static Token whitespace()
        {
            Token ret;
            ret.kind = TokenKind::Whitespace;
            return ret;
        }

        static Token raw(Char const *marker, size_t start, std::optional<size_t> end)
        {
            Token ret;
            ret.kind = TokenKind::Raw;
            ret.value.template emplace<RawText<Char>>(marker, start, end);
            return ret;
        }

        static Token tab()
        {
            Token ret;
            ret.kind = TokenKind::Tab;
            return ret;
        }

        static Token identifier()
        {
            Token ret;
            ret.kind = TokenKind::Identifier;
            return ret;
        }

        static Token comment(CommentType type, bool terminated = true)
        {
            Token ret;
            ret.kind = TokenKind::Comment;
            ret.value = CommentText { .comment_type = type, .terminated = terminated };
            return ret;
        }

        static Token end_of_line()
        {
            Token ret;
            ret.kind = TokenKind::EndOfLine;
            return ret;
        }

        static Token end_of_file()
        {
            Token ret;
            ret.kind = TokenKind::EndOfFile;
            return ret;
        }

        static Token string(QuoteType type, bool terminated = true, bool triple = false)
        {
            Token ret;
            ret.kind = TokenKind::QuotedString;
            ret.value = QuotedString {
                .quote_type = type,
                .triple = triple,
                .terminated = terminated
            };
            return ret;
        }

        Radix radix() const
        {
            assert(kind == TokenKind::Number);
            return std::get<Radix>(value);
        }

        Char symbol_code() const
        {
            assert(kind == TokenKind::Symbol);
            return std::get<Char>(value);
        }

        Keyword keyword() const
        {
            assert(kind == TokenKind::Keyword);
            return std::get<Keyword>(value);
        }

        QuotedString const &quoted_string() const
        {
            assert(kind == TokenKind::QuotedString);
            return std::get<QuotedString>(value);
        }

        CommentText const &comment_text() const
        {
            assert(kind == TokenKind::Comment);
            return std::get<CommentText>(value);
        }

        RawText<Char> const &raw_text() const
        {
            assert(kind == TokenKind::Raw);
            return std::get<RawText<Char>>(value);
        }

        bool operator==(TokenKind const &k) const
        {
            return k == kind;
        }

        bool operator!=(TokenKind const &k) const
        {
            return k != kind;
        }

        bool operator==(wchar_t s) const
        {
            return matches_symbol(s);
        }

        bool operator!=(wchar_t s) const
        {
            return !matches_symbol(s);
        }

        bool matches(TokenKind k) const { return kind == k; }
        bool matches_symbol(Char symbol) const { return matches(TokenKind::Symbol) && this->symbol_code() == symbol; }
        bool matches_keyword(Keyword kw) const { return matches(TokenKind::Keyword) && this->keyword() == kw; }
        bool is_identifier() const { return matches(TokenKind::Identifier); }
    };

    using LexerResult = std::expected<Token, LexerErrorMessage>;

    struct SkipToken {
        size_t index;
    };

    struct ScanResult {
        std::variant<Token, Buffer, SkipToken> result;
        size_t                                 matched;

        ScanResult() = default;
        ScanResult(ScanResult const &) = default;
        ScanResult &operator=(ScanResult const &) = default;

        template<typename T>
        ScanResult(T result, size_t matched)
            : result(std::move(result))
            , matched(matched)
        {
            // std::cout << "ScanResult matched " << this->matched << " ";
            // std::visit(overloads {
            //     [](Token const &token) {
            //         std::cout << " token [" << TokenKind_name(token.kind) << "]\n";
            //     },
            //     [](Buffer const &buffer) {
            //         std::cout << " buffer. " << buffer.length() << " chars\n";
            //     },
            //     [](SkipToken const &) {
            //         std::cout << " skip\n";
            //     }
            // }, this->result);
        }
    };

    template<typename... Scanners>
    struct ScannerPack {
        std::tuple<Scanners...> scanners {};

        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "ScannerPack<" << sizeof...(Scanners) << ">\n";
            return peek<Scanners...>(buffer, index);
        }

        template<typename S, typename... Ss>
        std::optional<ScanResult> peek(Buffer const &buffer, size_t index)
        {
            S &scanner = std::get<S>(scanners);
            if (auto res = scanner.scan(buffer, index)) {
                return *res;
            }
            if constexpr (sizeof...(Ss) > 0) {
                return peek<Ss...>(buffer, index);
            }
            return {};
        }
    };

    struct SlashSlash {
        constexpr static char const *value = "//";
    };

    struct HashmarkComments {
        constexpr static char const *value = "#";
    };

    template<typename Marker, bool Ignore = true>
    struct LineComments {
        constexpr static char const *marker = Marker::value;
        size_t                       length { 0 };

        LineComments()
        {
            length = strlen(marker);
        }

        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "LineComments<" << marker << ':' << length << ',' << Ignore << ">\n";
            auto ix = index;
            for (; ix < buffer.length() && ix - index < length; ++ix) {
                if (marker[ix - index] != buffer[ix]) {
                    return {};
                }
            }
            if (ix == buffer.length() && ix - index < length) {
                return {};
            }
            for (; ix < buffer.length() && buffer[ix] != '\n'; ++ix)
                ;
            if constexpr (Ignore) {
                return ScanResult { SkipToken { index }, ix - index };
            }
            return ScanResult { Token::comment(CommentType::Line), ix - index };
        }
    };

    struct CBlockComments {
        constexpr static char const *begin = "/*";
        constexpr static char const *end = "*/";
    };

    template<typename Markers, bool Ignore = true>
    struct BlockComments {
        constexpr static char const *begin = Markers::begin;
        constexpr static char const *end = Markers::end;
        size_t                       begin_length;
        size_t                       end_length;
        bool                         m_in_comment { false };

        BlockComments()
        {
            begin_length = strlen(begin);
            end_length = strlen(end);
        }

        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "BlockComments<" << begin << ':' << begin_length << ',' << end << ':' << end_length << ',' << Ignore << "> " << m_in_comment << "\n";
            auto cur = buffer[index];
            if (m_in_comment) {
                return block_comment(buffer, index);
            }
            auto ix = index;
            for (; ix < buffer.length() && ix - index < begin_length; ++ix) {
                if (begin[ix - index] != buffer[ix]) {
                    return {};
                }
            }
            if (ix == buffer.length() && ix - index < begin_length) {
                return {};
            }
            m_in_comment = true;
            return block_comment(buffer, index);
        }

        std::optional<ScanResult> block_comment(Buffer const &buffer, size_t index)
        {
            auto ix = index;
            for (; ix < buffer.length(); ++ix) {
                auto iix = ix;
                for (; iix < buffer.length() && iix - ix < end_length; ++iix) {
                    if (begin[iix - ix] != buffer[iix]) {
                        break;
                    }
                }
                if ((iix == buffer.length() && iix - ix < end_length) || buffer[iix] == '\n') {
                    if (iix == index) {
                        return {};
                    }
                    if constexpr (Ignore) {
                        return ScanResult { SkipToken { index }, ix - index };
                    }
                    return ScanResult { Token::comment(CommentType::Block, false), iix - index };
                }
                if (iix - ix == end_length) {
                    m_in_comment = false;
                    if constexpr (Ignore) {
                        return ScanResult { SkipToken { index }, ix - index };
                    }
                    return ScanResult { Token::comment(CommentType::Block, true), iix - index };
                }
            }
            UNREACHABLE();
        }
    };

    using CStyleComments = ScannerPack<LineComments<SlashSlash>, BlockComments<CBlockComments>>;

    template<typename Markers>
    struct RawScanner {
        constexpr static Char const *begin = Markers::begin;
        constexpr static Char const *end = Markers::end;
        size_t                       begin_length;
        size_t                       end_length;

        RawScanner()
        {
            if constexpr (typeid(Char) == typeid(char)) {
                begin_length = strlen(begin);
                end_length = strlen(end);
            }
            if constexpr (typeid(Char) == typeid(wchar_t)) {
                begin_length = wcslen(begin);
                end_length = wcslen(end);
            }
        }

        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            auto cur = buffer[index];
            auto ix = index;
            for (; ix < buffer.length() && ix - index < begin_length; ++ix) {
                if (begin[ix - index] != buffer[ix]) {
                    return {};
                }
            }
            if (ix == buffer.length() && ix - index < begin_length) {
                return {};
            }
            auto start = ix;
            for (; ix < buffer.length(); ++ix) {
                auto iix = ix;
                for (; iix < buffer.length() && iix - ix < end_length; ++iix) {
                    if (end[iix - ix] != buffer[iix]) {
                        break;
                    }
                    if (iix - ix + 1 == end_length) {
                        return ScanResult { Token::raw(begin, start, ix), iix - index + 1 };
                    }
                }
            }
            return ScanResult { Token::raw(begin, start, {}), ix - index };
        }
    };

    struct NumberScanner {
        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "NumberScanner\n";
            auto radix = Radix::Decimal;
            auto ix = index;
            auto cur = buffer[ix];
            if (!isdigit(cur)) {
                return {};
            }
            int (*predicate)(int) = isdigit;
            if (ix < buffer.length() - 1 && cur == '0') {
                if (buffer[ix + 1] == 'x' || buffer[ix + 1] == 'X') {
                    if (ix == buffer.length() - 2 || !isxdigit(buffer[ix + 2])) {
                        return ScanResult { Token::number(Radix::Decimal), ix - index + 1 };
                    }
                    radix = Radix::Hex;
                    predicate = isxdigit;
                    ix = ix + 2;
                } else if (buffer[ix + 1] == 'b' || buffer[ix + 1] == 'B') {
                    if (ix == buffer.length() - 2 || !isbdigit(buffer[ix + 2])) {
                        return ScanResult { Token::number(Radix::Decimal), ix - index + 1 };
                    }
                    radix = Radix::Binary;
                    predicate = isbdigit;
                    ix = ix + 2;
                }
            }
            for (; ix < buffer.length(); ++ix) {
                Char const ch = buffer[ix];
                if (!predicate(ch)) {
                    break;
                }
            }
            return ScanResult { Token::number(radix), ix - index };
        }
    };

    struct DefaultQuotes {
        constexpr static char const *quote_chars = "\"'`";
    };

    struct SingleDoubleQuotes {
        constexpr static char const *quote_chars = "\"'";
    };

    template<typename Quotes = DefaultQuotes>
    struct QuotedStringScanner {
        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "QuotedStringScanner\n";
            auto ix = index;
            auto cur = buffer[ix];
            if (strchr(Quotes::quote_chars, cur)) {
                ++ix;
                while (ix < buffer.length() && buffer[ix] != cur) {
                    ix += (buffer[ix] == '\\') ? 2 : 1;
                }
                if (ix < buffer.length()) {
                    ++ix;
                }
                return ScanResult { Token::string(static_cast<QuoteType>(cur), ix < buffer.length()), ix - index };
            }
            return {};
        }
    };

    template<bool Ignore = true>
    struct WhitespaceScanner {
        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "WhitespaceScanner\n";
            auto ix = index;
            auto cur = buffer[ix];
            switch (cur) {
            case '\n':
                if constexpr (Ignore) {
                    return ScanResult { SkipToken { index }, 1 };
                } else {
                    return ScanResult { Token::end_of_line(), 1 };
                }
            case '\t':
                if constexpr (Ignore) {
                    return ScanResult { SkipToken { index }, 1 };
                } else {
                    return ScanResult { Token::tab(), 1 };
                }
            case ' ':
                while (ix < buffer.length() && buffer[ix] == ' ') {
                    ++ix;
                }
                if constexpr (Ignore) {
                    return ScanResult { SkipToken { index }, ix - index };
                } else {
                    return ScanResult { Token::whitespace(), ix - index };
                }
            default:
                return {};
            }
        }
    };

    struct IdentifierScanner {
        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "IdentifierScanner\n";
            auto cur = buffer[index];
            if (isalpha(cur) || cur == '_') {
                std::string scanned;
                auto        ix = index;
                for (; isalnum(buffer[ix]) || buffer[ix] == '_'; ++ix) {
                    scanned += buffer[ix];
                }
                if (auto m = match_keyword<Keyword>(scanned); m && m->match_type == KWMatch::MatchType::FullMatch) {
                    // std::cout << "Identifier is keyword: " << scanned << std::endl;
                    return ScanResult { Token::keyword(m->keyword), scanned.length() };
                }
                // std::cout << "Identifier is not a keyword: " << scanned << std::endl;
                return ScanResult { Token::identifier(), scanned.length() };
            }
            return {};
        }
    };

    struct KeywordScanner {
        std::optional<ScanResult> scan(Buffer const &buffer, size_t index)
        {
            // std::cout << "KeywordScanner\n";
            std::string scanned;
            for (auto ix = index; ix < buffer.length(); ++ix) {
                scanned += buffer[ix];
                if (auto m = match_keyword<Keyword>(scanned); m) {
                    if (m->match_type == KWMatch::MatchType::FullMatch) {
                        // std::cout << "Standalone keyword: " << scanned << std::endl;
                        return ScanResult { Token::keyword(m->keyword), scanned.length() };
                    }
                } else {
                    break;
                }
            }
            return {};
        }
    };

    using CScannerPack = ScannerPack<CStyleComments,
        NumberScanner,
        QuotedStringScanner<SingleDoubleQuotes>,
        WhitespaceScanner<>,
        IdentifierScanner,
        KeywordScanner>;
};

template<typename Types, typename... Scanners>
class Lexer {
public:
    using Buffer = Types::Buffer;
    using Char = Types::Char;
    using Keyword = Types::Keyword;
    using Token = Types::Token;
    using ScanResult = Types::ScanResult;
    using LexerResult = Types::LexerResult;
    using SkipToken = Types::SkipToken;
    using LexerError = std::expected<void, LexerErrorMessage>;
    using Bookmark = size_t;

    Lexer() = default;

    void push_source(Buffer source)
    {
        m_sources.emplace_back(this, std::move(source));
    }

    std::basic_string_view<Char> text(Token const &token) const
    {
        return m_sources.back().substr(token.location.index, token.location.length);
    }

    std::string text_utf8(Token const &token) const
    {
        return as_utf8(text(token));
    }

    Token const &peek()
    {
        if (m_current.has_value()) {
            return m_current.value();
        }
        if (!pushed_back.empty()) {
            m_current = pushed_back.back();
            return m_current.value();
        }
        if (m_sources.empty()) {
            m_current = Token::end_of_file();
            return m_current.value();
        }
        while (!m_current.has_value()) {
            ScanResult res { m_sources.back().peek_next() };
            std::visit(overloads {
                           [this](Token const &token) -> void {
                               if (token.matches(TokenKind::EndOfFile)) {
                                   m_sources.pop_back();
                                   if (!exhausted()) {
                                       return;
                                   }
                               }
                               // std::cout << '[' << TokenKind_name(token.kind) << ' ' << token.location.index << ' ' << token.location.length << ' '
                               //      << as_utf8(m_sources.back().substr(token.location.index, token.location.length)) << ']' << std::endl;
                               m_current = token;
                           },
                           [this, &res](Buffer const &buffer) -> void {
                               push_source(std::get<Buffer>(res.result));
                           },
                           [](SkipToken) -> void {
                           } },
                res.result);
        }
        return m_current.value();
    }

    Token lex()
    {
        auto ret = peek();
        if (!pushed_back.empty()) {
            pushed_back.pop_back();
        } else if (!m_sources.empty()) {
            m_sources.back().lex();
        }
        m_current.reset();
        m_lookback.push_back(ret);
        last_location = ret.location;
        return ret;
    }

    Bookmark bookmark() const
    {
        trace("Bookmarking token #{}", m_lookback.size());
        return m_lookback.size();
    }

    void push_back(Bookmark const &bookmark)
    {
        while (m_lookback.size() != bookmark) {
            pushed_back.push_back(m_lookback.back());
            m_lookback.pop_back();
        }
    }

    Token const &lookback(size_t pos)
    {
        assert(pos < m_lookback.size());
        return m_lookback[m_lookback.size() - pos - 1];
    }

    bool has_lookback(size_t count)
    {
        return m_lookback.size() > count;
    }

    bool lookback_matches(size_t count, TokenKind kind)
    {
        if (!has_lookback(count)) {
            return false;
        }
        return lookback(count).matches(kind);
    }

    bool lookback_matches(size_t count, Keyword code)
    {
        if (!has_lookback(count)) {
            return false;
        }
        return lookback(count).matches_keyword(code);
    }

    LexerResult expect(TokenKind kind)
    {
        if (auto ret = peek(); !ret.matches(kind)) {
            return std::unexpected(LexerErrorMessage { location(),
                std::format("Expected '{}'", TokenKind_name(kind)) });
        }
        return lex();
    }

    bool accept(TokenKind kind)
    {
        if (auto ret = peek(); ret.matches(kind)) {
            lex();
            return true;
        }
        return false;
    }

    LexerError expect_keyword(Keyword code)
    {
        if (auto ret = peek(); !ret.matches_keyword(code)) {
            return std::unexpected<LexerErrorMessage>({
                location(),
                std::format("Expected keyword"), // FIXME need KW code -> text mechanism
            });
        }
        lex();
        return {};
    }

    bool accept_keyword(Keyword code)
    {
        if (auto ret = peek(); ret.matches_keyword(code)) {
            lex();
            return true;
        }
        return false;
    }

    LexerError expect_symbol(int symbol)
    {
        if (auto ret = peek(); !ret.matches_symbol(symbol)) {
            return std::unexpected<LexerErrorMessage>({
                location(),
                std::format("Expected '{}' but got '{}'", static_cast<char>(symbol), text_utf8(ret)),
            });
        }
        lex();
        return {};
    }

    bool accept_symbol(int symbol)
    {
        if (auto ret = peek(); ret.matches_symbol(symbol)) {
            lex();
            return true;
        }
        return false;
    }

    LexerResult expect_identifier()
    {
        if (auto ret = peek(); !ret.is_identifier()) {
            return std::unexpected(LexerErrorMessage { location(), "Expected identifier" });
        }
        return lex();
    }

    std::optional<Token> accept_identifier()
    {
        if (auto ret = peek(); !ret.is_identifier()) {
            return {};
        }
        return lex();
    }

    std::optional<Token> accept_number()
    {
        if (auto ret = peek(); ret.kind != TokenKind::Number) {
            return {};
        }
        return lex();
    }

    bool next_matches(TokenKind kind)
    {
        auto n = peek();
        return n.matches(kind);
    }

    bool next_matches(int symbol)
    {
        auto n = peek();
        return n.matches_symbol(symbol);
    }

    bool exhausted() const
    {
        return m_sources.empty();
    }

    void push_back(Token token)
    {
        if (!exhausted()) {
            m_sources.back().push_back(token);
        }
        pushed_back.emplace_back(std::move(token));
        m_current.reset();
        m_lookback.pop_back();
    }

    TokenLocation const &location() const
    {
        assert(!m_sources.empty());
        return m_sources.back().location();
    }

    TokenLocation last_location;

private:
    std::optional<Token> m_current {};

    class Source {
    public:
        TokenLocation const &location() const
        {
            return m_location;
        }

        Source(Lexer *lexer, Buffer const &src)
            : m_buffer(src)
            , m_lexer(lexer)
        {
        }

        void push_back(Token const &token)
        {
            m_index = token.location.index + token.location.length;
            m_location = token.location;
            m_location.index = m_index;
            if (token.matches(TokenKind::EndOfLine)) {
                m_location.line += 1;
                m_location.column = 0;
            } else {
                m_location.column += token.location.length;
            }
            m_current.reset();
        }

        ScanResult peek_next()
        {
            if (m_current.has_value()) {
                return { *m_current, m_current->location.length };
            }
            if (m_index >= m_buffer.length()) {
                return { Token::end_of_file(), 1 };
            }
            ScanResult ret { peek<Scanners...>() };
            m_index += ret.matched;
            m_location.length = ret.matched;
            std::visit(overloads {
                           [ret, this](Token &token) -> void {
                               token.location = m_location;
                           },
                           [](auto const &) -> void {
                           } },
                ret.result);
            for (; m_location.index < m_index; ++m_location.index) {
                if (m_buffer[m_location.index] == '\n') {
                    ++m_location.line;
                    m_location.column = 0;
                } else {
                    ++m_location.column;
                }
            }
            m_location.length = 0;
            return ret;
        }

        template<typename S, typename... Ss>
        ScanResult peek()
        {
            S &scanner = std::get<S>(m_scanners);
            if (std::optional<ScanResult> res = scanner.scan(m_buffer, m_index); res) {
                return *res;
            }
            if constexpr (sizeof...(Ss) > 0) {
                return peek<Ss...>();
            } else {
                return ScanResult { Token::symbol(static_cast<wchar_t>(m_buffer[m_index])), 1 };
            }
        }

        void lex()
        {
            if (!m_current) {
                return;
            }
            m_current.reset();
        }

        Char operator*() const
        {
            return m_buffer[m_index];
        }

        Char operator[](size_t ix) const
        {
            return m_buffer[ix];
        }

        std::basic_string_view<Char> substr(size_t pos, size_t len = std::basic_string_view<Char>::npos) const
        {
            return m_buffer.substr(pos, len);
        }

        size_t length() const
        {
            return m_buffer.length();
        }

        bool exhausted() const
        {
            return m_index >= length();
        }

        Source &operator++()
        {
            ++m_index;
            return *this;
        }

        Source &operator++(int)
        {
            ++m_index;
            return *this;
        }

    private:
        Buffer                  m_buffer;
        size_t                  m_index { 0 };
        Lexer                  *m_lexer;
        TokenLocation           m_location {};
        std::optional<Token>    m_current {};
        bool                    m_in_comment { false };
        std::string             m_scanned;
        std::tuple<Scanners...> m_scanners {};
    };

    std::deque<Token>   m_lookback {};
    std::deque<Token>   pushed_back {};
    std::vector<Source> m_sources {};
};
}
