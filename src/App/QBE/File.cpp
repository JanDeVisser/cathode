/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <ranges>
#include <sstream>

#include <App/Parser.h>
#include <App/QBE/QBE.h>
#include <App/Type.h>
#include <variant>

namespace Lia::QBE {

std::wstring type_ref(pType const &type)
{
    if (std::holds_alternative<SliceType>(type->description)) {
        return L":slice_t";
    }
    if (auto prefix = std::visit(
            overloads {
                [](OptionalType const &) -> wchar_t const * {
                    return L"opt";
                },
                [](StructType const &) -> wchar_t const * {
                    return L"struct";
                },
                [](ResultType const &) -> wchar_t const * {
                    return L"res";
                },
                [](TaggedUnionType const &) -> wchar_t const * {
                    return L"union";
                },
                [](auto const &) -> wchar_t const * {
                    return nullptr;
                } },
            type->description);
        prefix != nullptr) {
        return std::format(L":{}{}", prefix, *(type.id));
    } else {
        auto                t { qbe_type_code(type) };
        std::wostringstream ss;
        ss << t;
        return ss.str();
    }
}

void emit_type(pType const &type, std::wostream &os)
{
    std::visit(
        overloads {
            [&os](SliceType const &) {
                os << "type :slice_t = { l, l }\n";
            },
            [&os, &type](OptionalType const &opt) {
                os << "type "
                   << type_ref(type) << " = { "
                   << type_ref(opt.type) << ", b }\n";
            },
            [&os, &type](StructType const &strukt) {
                os << L"type " << type_ref(type) << " = { ";
                std::ranges::for_each(
                    strukt.fields | std::ranges::views::enumerate,
                    [&os](auto const &f) {
                        auto const &[ix, fld] = f;
                        if (ix > 0) {
                            os << ", ";
                        }
                        os << type_ref(fld.type);
                    });
                os << " }\n";
            },
            [&os, &type](ResultType const &result) {
                os << "type "
                   << type_ref(type) << "$_union = { { "
                   << type_ref(result.success) << " } { "
                   << type_ref(result.error) << " } }\n";
                os << "type "
                   << type_ref(type) << " = { "
                   << type_ref(type) << "$_union, b }\n";
            },
            [&os, &type](TaggedUnionType const &tagged_union) {
                os << "type "
                   << type_ref(type) << "$_union = { ";
                std::ranges::for_each(
                    tagged_union.tags,
                    [&os](TaggedUnionType::Tag const &tag) {
                        if (tag.payload != nullptr && tag.payload != TypeRegistry::void_) {
                            os << "{ "
                               << type_ref(tag.payload)
                               << " } ";
                        }
                    });
                os << " }\n"
                   << "type "
                   << type_ref(type) << " = { "
                   << type_ref(type) << "$_union, "
                   << type_ref(tagged_union.underlying())
                   << " }\n";
            },
            [](auto const &) {
            } },
        type->description);
}

std::wostream &operator<<(std::wostream &os, ILFile const &file)
{
    for (auto const type : file.types) {
        emit_type(type, os);
    }
    if (!file.types.empty()) {
        os << '\n';
    }
    os << DbgFile { file.name } << '\n';
    for (auto const &function : file.functions) {
        os << function;
    }
    for (auto const &[ix, glb] : std::ranges::views::enumerate(file.globals)) {
        os << "data $" << glb.name << " = { ";
        std::visit(
            overloads {
                [&os](ILValue::ILValues const &values) {
                    auto first { true };
                    for (auto const &v : values) {
                        if (!first) {
                            os << ", ";
                        }
                        first = false;
                        os << v.type << ' ' << v;
                    }
                },
                [&os, &glb](auto const &) {
                    os << glb.value.type << ' ' << glb.value;
                } },
            glb.value.inner);
        os << " }\n";
    }
    for (auto const &[ix, s] : std::ranges::views::enumerate(file.strings)) {
        os << "data $str_" << ix << " = { ";
        for (auto ch : s) {
            os << std::format(L"w {:d}, ", ch);
        }
        os << "w 0 }\n";
    }
    for (auto const &[ix, s] : std::ranges::views::enumerate(file.cstrings)) {
        os << "data $cstr_" << ix << " = { ";
        for (auto ch : s) {
            os << std::format(L"b {:d}, ", ch);
        }
        os << L"b 0 }\n";
    }
    if (!file.enumerations.empty()) {
        os << '\n';
        std::ranges::for_each(
            file.enumerations | std::views::enumerate,
            [&os](auto const &tuple) {
                auto const &[enum_id, enum_type] = tuple;
                assert(std::holds_alternative<EnumType>(enum_type->description));
                auto const &e { std::get<EnumType>(enum_type->description) };
                os << "data $enum$_" << enum_id + 1 << " = { l " << e.values.size() << ", ";
                std::ranges::for_each(
                    e.values,
                    [&os](EnumType::Value const &v) {
                        os << "l " << v.value << ", l " << v.label.length() << ", ";
                    });
                std::ranges::for_each(
                    e.values | std::views::enumerate,
                    [&os](auto const &tuple) {
                        auto &[ix, v] = tuple;
                        if (ix > 0) {
                            os << ", ";
                        }
                        for (auto ch : v.label) {
                            os << std::format(L"w {:d}, ", ch);
                        }
                        os << "w 0";
                    });
                os << " }\n";
            });
    }
    return os;
}

ILBinding const &ILFile::add(std::wstring_view name, pType type, ILValue init)
{
    auto  value { ILValue::global(std::wstring { name }, type) };
    auto &ret { globals.emplace_back(std::wstring { name }, std::move(type), globals.size(), std::move(init)) };
    return ret;
}

}
