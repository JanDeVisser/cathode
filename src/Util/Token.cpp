/*
 * Copyright (c) 2024, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/Lexer.h>

namespace Util {

std::string QuoteType_name(QuoteType type)
{
    switch (type) {
#undef S
#define S(T, Q)        \
    case QuoteType::T: \
        return std::format("{:}", Q);
        QUOTETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

EnumResult<QuoteType> QuoteType_from_string(std::string_view const &type)
{
    if (type.length() != 1) {
        return std::unexpected(NoSuchEnumValue { "QuoteType", std::string(type) });
    }
#undef S
#define S(T, Q)       \
    if (type[0] == Q) \
        return QuoteType::T;
    QUOTETYPES(S)
#undef S
    return std::unexpected(NoSuchEnumValue { "QuoteType", std::string(type) });
}

std::string CommentType_name(CommentType type)
{
    switch (type) {
#undef COMMENTTYPE_ENUM
#define S(T)             \
    case CommentType::T: \
        return #T;
        COMMENTTYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

EnumResult<CommentType> CommentType_from_string(std::string_view const &type)
{
#undef S
#define S(T)        \
    if (type == #T) \
        return CommentType::T;
    COMMENTTYPES(S)
#undef S
    return std::unexpected(NoSuchEnumValue { "CommentType", std::string(type) });
}

}
