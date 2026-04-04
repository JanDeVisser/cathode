/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <optional>

#include <Util/Lexer.h>

namespace Lia {

using namespace Util;

using Precedence = uint16_t;

#define LiaKeywords(S)         \
    S(Apply, "=>")             \
    S(AssignAnd, "&=")         \
    S(AssignDecrement, "-=")   \
    S(AssignDivide, "/=")      \
    S(AssignIncrement, "+=")   \
    S(AssignModulo, "%=")      \
    S(AssignMultiply, "*=")    \
    S(AssignOr, "|=")          \
    S(AssignShiftLeft, "<<=")  \
    S(AssignShiftRight, ">>=") \
    S(AssignXor, "^=")         \
    S(Alias, "alias")          \
    S(Break, "break")          \
    S(Cast, "::")              \
    S(Const, "const")          \
    S(Continue, "continue")    \
    S(Defer, "defer")          \
    S(Else, "else")            \
    S(Embed, "@embed")         \
    S(Enum, "enum")            \
    S(Equals, "==")            \
    S(Error, "error")          \
    S(Export, "export")        \
    S(Extern, "extern")        \
    S(ExternLink, "->")        \
    S(False, "false")          \
    S(For, "for")              \
    S(Func, "func")            \
    S(GreaterEqual, ">=")      \
    S(If, "if")                \
    S(Import, "import")        \
    S(Include, "@include")     \
    S(LessEqual, "<=")         \
    S(LogicalAnd, "&&")        \
    S(LogicalOr, "||")         \
    S(Loop, "loop")            \
    S(Must, "must")            \
    S(NotEqual, "!=")          \
    S(Null, "null")            \
    S(Public, "public")        \
    S(Range, "..")             \
    S(Return, "return")        \
    S(ShiftLeft, "<<")         \
    S(ShiftRight, ">>")        \
    S(Sizeof, "#::")           \
    S(Struct, "struct")        \
    S(True, "true")            \
    S(While, "while")          \
    S(Yield, "yield")

enum class LiaKeyword {
#undef S
#define S(KW, S) KW,
    LiaKeywords(S)
#undef S
};

struct LiaError {
    TokenLocation location;
    std::wstring  message;
};

extern char const *LiaKeyword_name(LiaKeyword kw);

}

namespace Util {

using namespace Lia;

using LiaKeywordMatch = KeywordMatch<LiaKeyword>;

template<>
[[nodiscard]] inline std::optional<LiaKeywordMatch> match_keyword(std::string const &str)
{
#undef S
#define S(KW, STR)                                                                                                                  \
    {                                                                                                                               \
        std::string_view kw_str { STR };                                                                                            \
        if (kw_str.starts_with(str)) {                                                                                              \
            return LiaKeywordMatch {                                                                                                \
                LiaKeyword::KW,                                                                                                     \
                (str.length() == kw_str.length()) ? LiaKeywordMatch::MatchType::FullMatch : LiaKeywordMatch::MatchType::PrefixMatch \
            };                                                                                                                      \
        }                                                                                                                           \
    }
    LiaKeywords(S)
#undef S
        return {};
}

}
