/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

#include <Lang/QBE/QBE.h>

namespace Lang {

using namespace std::literals;

Alias::Alias(std::wstring name, ASTNode aliased_type)
    : name(std::move(name))
    , aliased_type(std::move(aliased_type))
{
}

ASTNode Alias::normalized(ASTNode const &n) const
{
    return make_node<Alias>(n, name, normalize(aliased_type));
}

BindResult Alias::bind(ASTNode const &n) const
{
    Parser &parser { *n.repo };
    try_bind(aliased_type);
    if (aliased_type->bound_type != nullptr) {
        auto aliased_type_type { get<TypeType>(aliased_type->bound_type).type };
        auto type { TypeRegistry::the().alias_for(aliased_type_type) };
        parser.register_type(name, type);
        return make_type(name, TypeType { .type = type });
    }
    return nullptr;
}

Enum::Enum(std::wstring name, ASTNode underlying_type, ASTNodes values)
    : name(std::move(name))
    , underlying_type(std::move(underlying_type))
    , values(std::move(values))
{
}

ASTNode Enum::normalized(ASTNode const &n) const
{
    return make_node<Enum>(n, name, normalize(underlying_type), normalize(values));
}

BindResult Enum::bind(ASTNode const &n) const
{
    auto &parser = *(n.repo);

    auto is_tagged_union = std::ranges::any_of(
        values,
        [](ASTNode const &v) -> bool { return std::get<EnumValue>(v->node).payload != nullptr; });

    pType underlying_type_type { nullptr };
    if (underlying_type != nullptr) {
        underlying_type_type = resolve(underlying_type);
        if (underlying_type_type == nullptr) {
            return n.bind_error(L"Could not resolve type `{}`", underlying_type);
        }
    }

    pType    enum_type { nullptr };
    EnumType enoom;
    if (underlying_type_type != nullptr && is<EnumType>(underlying_type_type)) {
        if (!is_tagged_union) {
            return n.bind_error(L"Attempt to create enum with existing enum `{}` as underlying type", underlying_type_type);
        }
        enoom = get<EnumType>(underlying_type_type);
        enum_type = underlying_type_type;
    } else if (underlying_type_type == nullptr || is<IntType>(underlying_type_type)) {
        if (underlying_type_type == nullptr) {
            underlying_type_type = TypeRegistry::i32;
        }
        enoom.underlying_type = underlying_type_type;
        int64_t value { -1 };
        for (auto const v : values) {
            auto enum_value = get<EnumValue>(v);
            if (enum_value.value != nullptr) {
                value = get<int64_t>(get<Number>(enum_value.value));
            } else {
                ++value;
            }
            enoom.values.emplace_back(enum_value.label, value);
        }
    } else if (is_tagged_union) {
        return n.bind_error(L"Invalid underlying type `{}` for tagged union", underlying_type_type);
    } else {
        return n.bind_error(L"Invalid underlying type `{}` for enum", underlying_type_type);
    }
    if (!is_tagged_union) {
        auto ret = make_type(name, enoom);
        parser.register_type(name, ret);
        return make_type(name, TypeType { .type = ret });
    }
    if (enum_type == nullptr) {
        enum_type = make_type(std::format(L"$enum${}", name), enoom);
    }
    TaggedUnionType tagged_union;
    for (auto const &[ix, v] : std::ranges::views::enumerate(values)) {
        pType payload { TypeRegistry::void_ };
        auto  tagged_value = get<EnumValue>(v);
        if (tagged_value.payload != nullptr) {
            payload = resolve(tagged_value.payload);
            if (payload == nullptr) {
                return n.bind_error(L"Could not resolve type `{}`", to_string(tagged_value.payload));
            }
        }
        auto sz = tagged_union.tags.size();
        for (auto const &ev : enoom.values) {
            if (ev.label == tagged_value.label) {
                tagged_union.tags.emplace_back(ev.value, payload);
                break;
            }
        }
        if (sz == tagged_union.tags.size()) {
            return n.bind_error(L"Unrecognized enum label `{}` in definition of of tagged union", tagged_value.label);
        }
    }
    tagged_union.tag_type = enum_type;
    auto ret = make_type(name, tagged_union);
    parser.register_type(name, ret);
    return make_type(name, TypeType { .type = ret });
}

EnumValue::EnumValue(std::wstring label, ASTNode value, ASTNode payload)
    : label(std::move(label))
    , value(std::move(value))
    , payload(std::move(payload))
{
}

ASTNode EnumValue::normalized(ASTNode const &n) const
{
    return make_node<EnumValue>(n, label, normalize(value), normalize(payload));
}

BindResult EnumValue::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

Struct::Struct(std::wstring name, ASTNodes members)
    : name(std::move(name))
    , members(std::move(members))
{
}

ASTNode Struct::normalized(ASTNode const &n) const
{
    return make_node<Struct>(n, name, normalize(members));
}

BindResult Struct::bind(ASTNode const &n) const
{
    auto              &parser { *(n.repo) };
    StructType::Fields fields;
    for (auto const &m : members) {
        try_bind(m);
        fields.emplace_back(get<StructMember>(m).label, m->bound_type);
    }
    pType type = TypeRegistry::the().struct_of(fields);
    parser.register_type(name, type);
    return make_type(name, TypeType { .type = type });
}

StructMember::StructMember(std::wstring label, ASTNode type)
    : label(std::move(label))
    , member_type(std::move(type))
{
    assert(this->member_type != nullptr);
}

ASTNode StructMember::normalized(ASTNode const &n) const
{
    return make_node<StructMember>(n, label, normalize(member_type));
}

BindResult StructMember::bind(ASTNode const &) const
{
    return get<TypeType>(try_bind(member_type)).type;
}

TagValue::TagValue(int64_t tag_value, std::wstring label, pType payload_type, ASTNode payload)
    : tag_value(tag_value)
    , label(std::move(label))
    , payload_type(std::move(payload_type))
    , payload(std::move(payload))
{
}

TagValue::TagValue(ASTNode operand, int64_t tag_value, std::wstring label, pType payload_type, ASTNode payload)
    : operand(std::move(operand))
    , tag_value(tag_value)
    , label(std::move(label))
    , payload_type(std::move(payload_type))
    , payload(std::move(payload))
{
}

TypeSpecification::TypeSpecification(TypeSpecificationDescription description)
    : description(description)
{
}

ASTNode TypeSpecification::normalized(ASTNode const &n) const
{
    auto normalized_description = std::visit(
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
            [](PointerDescriptionNode const &d) -> TypeSpecificationDescription {
                return PointerDescriptionNode { normalize(d.referencing) };
            },
            [](ResultDescriptionNode const &d) -> TypeSpecificationDescription {
                return ResultDescriptionNode {
                    normalize(d.success),
                    normalize(d.error),
                };
            },
        },
        description);
    return make_node<TypeSpecification>(n, normalized_description);
}

BindResult TypeSpecification::bind(ASTNode const &n) const
{
    auto ret = resolve(n);
    if (ret == nullptr) {
        return BindError { ASTStatus::Undetermined };
    }
    return TypeRegistry::the().type_of(ret);
}

Void::Void()
{
}

BindResult Void::bind(ASTNode const &) const
{
    return TypeRegistry::the().type_of(TypeRegistry::void_);
}

}
