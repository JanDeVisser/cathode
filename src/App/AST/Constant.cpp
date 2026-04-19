/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/QBE/QBE.h>

namespace Lia {

using namespace std::literals;

BoolConstant::BoolConstant(bool value)
    : value(value)
{
}

BindResult BoolConstant::bind(ASTNode const &) const
{
    return TypeRegistry::boolean;
}

CString::CString(std::string string)
    : string(std::move(string))
{
}

BindResult CString::bind(ASTNode const &) const
{
    return TypeRegistry::cstring;
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
    assert(dbl.has_value());
    value = *dbl;
}

BindResult Decimal::bind(ASTNode const &) const
{
    return TypeRegistry::f64;
}

BindResult Dummy::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

Nullptr::Nullptr()
{
}

BindResult Nullptr::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
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

BindResult Number::bind(ASTNode const &) const
{
    return std::visit(
        [](auto v) -> pType {
            return type_of<decltype(v)>();
        },
        value);
}

QuotedString::QuotedString(std::wstring_view str, QuoteType type)
    : string(str)
    , quote_type(type)
{
}

String::String(std::wstring string)
    : string(std::move(string))
{
}

BindResult String::bind(ASTNode const &) const
{
    return TypeRegistry::string;
}

}
