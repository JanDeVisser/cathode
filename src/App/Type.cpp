/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

#include <Util/Logging.h>
#include <Util/StringUtil.h>
#include <Util/TokenLocation.h>

#include <App/Type.h>

namespace Lia {

using namespace std::literals;
using namespace Util;

IntType   IntType::u8 { false, 8, std::numeric_limits<uint8_t>::max(), std::numeric_limits<uint8_t>::min() };
IntType   IntType::u16 { false, 16, std::numeric_limits<uint16_t>::max(), std::numeric_limits<uint16_t>::min() };
IntType   IntType::u32 { false, 32, std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint32_t>::min() };
IntType   IntType::u64 { false, 64, std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint64_t>::min() };
IntType   IntType::i8 { true, 8, std::numeric_limits<int8_t>::max(), std::numeric_limits<int8_t>::min() };
IntType   IntType::i16 { true, 16, std::numeric_limits<int16_t>::max(), std::numeric_limits<int16_t>::min() };
IntType   IntType::i32 { true, 32, std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min() };
IntType   IntType::i64 { true, 64, std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min() };
FloatType FloatType::f32 { 32 };
FloatType FloatType::f64 { 64 };

pType TypeRegistry::u8;
pType TypeRegistry::u16;
pType TypeRegistry::u32;
pType TypeRegistry::u64;
pType TypeRegistry::i8;
pType TypeRegistry::i16;
pType TypeRegistry::i32;
pType TypeRegistry::i64;
pType TypeRegistry::f32;
pType TypeRegistry::f64;
pType TypeRegistry::boolean;
pType TypeRegistry::string;
pType TypeRegistry::string_builder;
pType TypeRegistry::cstring;
pType TypeRegistry::character;
pType TypeRegistry::void_;
pType TypeRegistry::pointer;

TypeRegistry TypeRegistry::s_registry {};

std::wstring type_name(pType const &type)
{
    return type ? (type->name) : L"nullptr";
}

std::wstring IntType::to_string() const
{
    return std::format(L"{:c}{}", (is_signed) ? 'i' : 'u', width_bits);
}

std::wstring IntType::encode() const
{
    return std::format(L"{:1x}", (size_of() - 1) | (is_signed ? 1 : 0));
}

std::wstring FloatType::to_string() const
{
    return std::format(L"f{}", width_bits);
}

std::wstring FloatType::encode() const
{
    return std::format(L"{:1x}", size_of());
}

std::wstring FunctionType::to_string() const
{
    return std::format(L"Func{} {}", parameters->name, result->name);
}

std::wstring FunctionType::encode() const
{
    return std::format(L"{}{}", parameters->encode(), result->encode());
}

std::wstring TypeList::to_string() const
{
    return std::format(L"({})", join_elements(types, L", "sv, [](pType const &t) { return type_name(t); }));
}

intptr_t TypeList::size_of() const
{
    intptr_t ret { 0 };
    std::ranges::for_each(types, [&ret](pType const &t) {
        ret = alignat(ret, t->align_of()) + t->size_of();
    });
    return ret;
}

intptr_t TypeList::align_of() const
{
    intptr_t ret { 0 };
    std::ranges::for_each(types, [&ret](pType const &t) {
        ret = std::max(ret, t->align_of());
    });
    return ret;
}

std::wstring TypeList::encode() const
{
    std::wstring ret { std::format(L"{:1x}", types.size()) };
    std::ranges::for_each(
        types,
        [&ret](pType const &t) {
            ret += t->encode();
        });
    return ret;
}

std::wstring ReferenceType::to_string() const
{
    return std::format(L"&{}", type_name(referencing));
}

std::wstring ReferenceType::encode() const
{
    return referencing->encode();
}

std::wstring SliceType::to_string() const
{
    return std::format(L"SliceOf({})", type_name(slice_of));
}

std::wstring SliceType::encode() const
{
    return slice_of->encode();
}

std::wstring ZeroTerminatedArray::to_string() const
{
    return std::format(L"ZeroTerminatedArrayOf({})", type_name(array_of));
}

std::wstring ZeroTerminatedArray::encode() const
{
    return array_of->encode();
}

std::wstring Array::to_string() const
{
    return std::format(L"ArrayOf({}[{}])", type_name(array_of), size);
}

std::wstring Array::encode() const
{
    return array_of->encode();
}

intptr_t Array::size_of() const
{
    return size * array_of->size_of();
}

intptr_t Array::align_of() const
{
    return array_of->align_of();
}

std::wstring DynArray::to_string() const
{
    return std::format(L"DynArrayOf({})", type_name(array_of));
}

std::wstring DynArray::encode() const
{
    return array_of->encode();
}

std::wstring RangeType::to_string() const
{
    return std::format(L"RangeOf({})", type_name(range_of));
}

std::wstring RangeType::encode() const
{
    return range_of->encode();
}

intptr_t RangeType::size_of() const
{
    return 3 * range_of->size_of();
}

intptr_t RangeType::align_of() const
{
    return range_of->align_of();
}

std::wstring TypeAlias::to_string() const
{
    return std::format(L"AliasOf({})", type_name(alias_of));
}

intptr_t TypeAlias::size_of() const
{
    return alias_of->size_of();
}

intptr_t TypeAlias::align_of() const
{
    return alias_of->align_of();
}

std::wstring TypeAlias::encode() const
{
    return alias_of->encode();
}

std::wstring EnumType::to_string() const
{
    return std::format(L"Enum({} values)", values.size());
}

std::wstring EnumType::encode() const
{
    return std::format(L"{}{:1x}", underlying_type->encode(), values.size());
}

intptr_t EnumType::size_of() const
{
    return underlying_type->size_of();
}

intptr_t EnumType::align_of() const
{
    return underlying_type->align_of();
}

bool EnumType::is_valid(std::wstring_view label) const
{
    return std::ranges::any_of(
        values,
        [&label](auto const &value) -> bool {
            return value.label == label;
        });
}

std::optional<int64_t> EnumType::value_for(std::wstring_view label) const
{
    auto const &res = std::ranges::find_if(
        values,
        [&label](auto const &v) -> bool {
            return v.label == label;
        });
    if (res != values.end()) {
        return res->value;
    }
    return {};
}

pType EnumType::underlying() const
{
    return underlying_type;
}

std::wstring TaggedUnionType::to_string() const
{
    return std::format(L"TaggedUnion({} tags)", tags.size());
}

std::wstring TaggedUnionType::encode() const
{
    auto ret { tag_type->encode() };
    std::for_each(
        tags.begin(),
        tags.end(),
        [&ret, this](Tag const &tag) -> void {
            if (tag.payload) {
                ret += tag.payload->encode();
            } else {
                ret += TypeRegistry::the().void_->encode();
            }
        });
    return ret;
}

intptr_t TaggedUnionType::size_of() const
{
    intptr_t maxsize { 0 };
    std::for_each(
        tags.begin(),
        tags.end(),
        [&maxsize, this](Tag const &tag) -> void {
            if (tag.payload) {
                maxsize = std::max(alignat(tag.payload->size_of(), align_of()), maxsize);
            }
        });
    maxsize = alignat(maxsize, tag_type->align_of());
    return maxsize + tag_type->size_of();
}

intptr_t TaggedUnionType::align_of() const
{
    intptr_t ret { tag_type->align_of() };
    std::for_each(
        tags.begin(),
        tags.end(),
        [&ret](Tag const &tag) -> void {
            ret = std::max((tag.payload) ? tag.payload->align_of() : 0, ret);
        });
    return ret;
}

bool TaggedUnionType::is_valid(std::wstring_view label) const
{
    return get<EnumType>(tag_type).is_valid(label);
}

std::optional<int64_t> TaggedUnionType::value_for(std::wstring_view label) const
{
    return get<EnumType>(tag_type).value_for(label);
}

pType TaggedUnionType::payload_for(std::wstring_view label) const
{
    auto v = value_for(label);
    if (!v) {
        return nullptr;
    }
    return payload_for(*v);
}

pType TaggedUnionType::payload_for(int64_t tag) const
{
    for (auto const &t : tags) {
        if (t.value == tag) {
            return t.payload;
        }
    }
    return TypeRegistry::void_;
}

pType TaggedUnionType::underlying() const
{
    return get<EnumType>(tag_type).underlying();
}

intptr_t TaggedUnionType::tag_offset() const
{
    intptr_t ret { 0 };
    for (auto const &tag : tags) {
        if (tag.payload->size_of() > ret) {
            ret = tag.payload->size_of();
        }
    }
    return alignat(ret, tag_type->align_of());
}

std::wstring PointerType::to_string() const
{
    return std::format(L"Pointer({})", referencing->to_string());
}

std::wstring PointerType::encode() const
{
    return referencing->encode();
}

std::wstring StructType::to_string() const
{
    return std::format(L"Struct({} fields)", fields.size());
}

std::wstring StructType::encode() const
{
    auto ret { std::format(L"{:1x}", fields.size()) };
    std::ranges::for_each(
        fields,
        [&ret, this](Field const &fld) -> void {
            ret += fld.type->encode();
        });
    return ret;
}

intptr_t StructType::size_of() const
{
    intptr_t size { 0 };
    std::ranges::for_each(fields,
        [&size](Field const &fld) -> void {
            size = alignat(size, fld.type->align_of()) + fld.type->size_of();
        });
    return size;
}

intptr_t StructType::align_of() const
{
    intptr_t ret { 0 };
    std::for_each(
        fields.begin(),
        fields.end(),
        [&ret](Field const &fld) -> void {
            ret = std::max(fld.type->align_of(), ret);
        });
    return ret;
}

size_t StructType::offset_of(std::wstring_view field_name) const
{
    intptr_t offset { 0 };
    assert(std::ranges::find_if(fields.begin(), fields.end(),
               [&offset, &field_name](Field const &fld) -> bool {
                   if (field_name == fld.name) {
                       return true;
                   }
                   offset = alignat(offset, fld.type->align_of()) + fld.type->size_of();
                   return false;
               })
        != fields.end());
    return offset;
}

std::wstring OptionalType::to_string() const
{
    return std::format(L"OptionalOf({})", type_name(type));
}

std::wstring OptionalType::encode() const
{
    return type->encode();
}

intptr_t OptionalType::size_of() const
{
    return TypeRegistry::boolean->size_of() + type->size_of();
}

intptr_t OptionalType::align_of() const
{
    return type->align_of();
}

std::wstring ResultType::to_string() const
{
    return std::format(L"Result({}/{})", type_name(success), type_name(error));
}

std::wstring ResultType::encode() const
{
    return success->encode() + error->encode();
}

intptr_t ResultType::size_of() const
{
    return flag_offset() + TypeRegistry::boolean->size_of();
}

intptr_t ResultType::align_of() const
{
    return std::max(success->align_of(), error->align_of());
}

intptr_t ResultType::flag_offset() const
{
    return alignat(
        std::max(success->size_of(), error->size_of()),
        std::max(success->align_of(), error->align_of()));
}

std::wstring TypeType::to_string() const
{
    return std::format(L"TypeOf({})", type->to_string());
}

std::wstring TypeType::encode() const
{
    return type->encode();
}

bool Type::is_a(TypeKind kind) const
{
    return description.index() == static_cast<int>(kind);
}

TypeKind Type::kind() const
{
    return static_cast<TypeKind>(description.index());
}

std::wstring Type::to_string() const
{
    return std::format(
        L"{}: {}",
        name,
        std::visit(
            overloads {
                [](std::monostate const &) -> std::wstring {
                    UNREACHABLE();
                    return L"";
                },
                [](auto const &descr) -> std::wstring {
                    return descr.to_string();
                } },
            description));
}

std::wstring Type::encode() const
{
    return std::format(
        L"{:c}{}",
        static_cast<int>(kind()) + 'a',
        std::visit(
            overloads {
                [](std::monostate const &) -> std::wstring {
                    UNREACHABLE();
                    return L"";
                },
                [](auto const &descr) -> std::wstring {
                    return descr.encode();
                } },
            description));
}

intptr_t Type::size_of() const
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> intptr_t {
                UNREACHABLE();
                return 0;
            },
            [](auto const &descr) -> intptr_t {
                return descr.size_of();
            } },
        description);
}

intptr_t Type::align_of() const
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> intptr_t {
                UNREACHABLE();
                return 0;
            },
            [](auto const &descr) -> intptr_t {
                return descr.align_of();
            } },
        description);
}

bool Type::compatible(pType const &other) const
{
    if (id == other) {
        return true;
    }
    if (is<ReferenceType>(id)) {
        return get<ReferenceType>(id).referencing->compatible(other);
    }
    if (is<ReferenceType>(other)) {
        return compatible(get<ReferenceType>(other).referencing);
    }
    auto left = (description.index() <= other->description.index()) ? id : other;
    auto right = (description.index() <= other->description.index()) ? other : id;
    return std::visit(
        overloads {
            [this, &other](TypeList const &list, SliceType const &slice) -> bool {
                return std::ranges::all_of(list.types, [&slice](auto const &t) {
                    return t->compatible(slice.slice_of);
                });
            },
            [this, &other](TypeList const &list, DynArray const &dynarr) -> bool {
                return std::ranges::all_of(list.types, [&dynarr](auto const &t) {
                    return t->compatible(dynarr.array_of);
                });
            },
            [this, &other](TypeList const &list, Array const &arr) -> bool {
                if (list.types.size() != arr.size) {
                    return false;
                }
                return std::ranges::all_of(list.types, [&arr](auto const &t) {
                    return t->compatible(arr.array_of);
                });
            },
            [this, &other](TypeList const &list, ZeroTerminatedArray const &arr) -> bool {
                return std::ranges::all_of(list.types, [&arr](auto const &t) {
                    return t->compatible(arr.array_of);
                });
            },
            [this, &other](TypeList const &list, StructType const &strukt) -> bool {
                if (list.types.size() != strukt.fields.size()) {
                    return false;
                }
                return std::ranges::all_of(std::ranges::views::zip(list.types, strukt.fields), [](auto const &tuple) {
                    return std::get<0>(tuple)->compatible(std::get<1>(tuple).type);
                });
            },
            [](auto const &, std::monostate const &) -> bool {
                UNREACHABLE();
                return false;
            },
            [this, &other](auto const &, auto const &) -> bool {
                return false;
            } },
        left->description, right->description);
}

bool Type::assignable_to(pType const &lhs) const
{
    if (id == lhs) {
        return true;
    }
    auto rhs = id;
    return std::visit(
        overloads {
            [](OptionalType const &, VoidType const &) -> bool {
                return true;
            },
            [](BoolType const &, OptionalType const &) -> bool {
                return true;
            },
            [](BoolType const &, ResultType const &) -> bool {
                return true;
            },
            [this, &rhs](OptionalType const &optional, auto const &) -> bool {
                return optional.type == rhs;
            },
            [this, &rhs](ResultType const &result, auto const &) -> bool {
                return result.success == rhs || result.error == rhs;
            },
            [&lhs](ReferenceType const &ref, auto const &) -> bool {
                return ref.referencing->assignable_to(lhs);
            },
            [this, &lhs](auto const &, auto const &) -> bool {
                return false;
            } },
        lhs->description, description);
}

pType Type::value_type() const
{
    auto val_type { id };
    if (val_type->kind() == TypeKind::ReferenceType) {
        val_type = std::get<ReferenceType>(val_type->description).referencing;
    }
    return val_type;
}

std::map<std::wstring, pType> Type::infer_generic_arguments(pType const &param_type) const
{
    std::function<void(std::map<std::wstring, pType> &, pType const &, pType const &)> infer;
    infer = [&infer](std::map<std::wstring, pType> &mapping, pType const &arg, pType const &param) {
        std::visit(
            overloads {
                [&mapping, &infer](OptionalType const &d, OptionalType const &other) -> void {
                    infer(mapping, d.type, other.type);
                },
                [&mapping, &infer](ResultType const &d, ResultType const &other) -> void {
                    infer(mapping, d.success, other.success);
                    infer(mapping, d.error, other.error);
                },
                [&mapping, &infer](SliceType const &d, SliceType const &other) -> void {
                    infer(mapping, d.slice_of, other.slice_of);
                },
                [&mapping, &infer](Array const &d, Array const &other) -> void {
                    infer(mapping, d.array_of, other.array_of);
                },
                [&mapping, &infer](DynArray const &d, DynArray const &other) -> void {
                    infer(mapping, d.array_of, other.array_of);
                },
                [&mapping, &infer](ZeroTerminatedArray const &d, ZeroTerminatedArray const &other) -> void {
                    infer(mapping, d.array_of, other.array_of);
                },
                [&mapping, &infer](RangeType const &d, RangeType const &other) -> void {
                    infer(mapping, d.range_of, other.range_of);
                },
                [&mapping, &infer, &arg](auto const &d, TypeAlias const &other) -> void {
                    infer(mapping, arg, other.alias_of);
                },
                [&mapping, &arg](auto const &d, GenericParameter const &generic) -> void {
                    mapping[generic.name] = arg;
                },
                [](auto const &d, auto const &other) -> void {
                } },
            arg->description, param->description);
    };
    if (std::holds_alternative<TypeAlias>(description)) {
        return std::get<TypeAlias>(description).alias_of->infer_generic_arguments(param_type);
    }
    std::map<std::wstring, pType> ret;
    infer(ret, this->id, param_type);
    return ret;
}

TypeRegistry::TypeRegistry()
{
    u8 = make_type(L"u8", IntType::u8);
    u16 = make_type(L"u16", IntType::u16);
    u32 = make_type(L"u32", IntType::u32);
    u64 = make_type(L"u64", IntType::u64);
    i8 = make_type(L"i8", IntType::i8);
    i16 = make_type(L"i16", IntType::i16);
    i32 = make_type(L"i32", IntType::i32);
    i64 = make_type(L"i64", IntType::i64);
    f32 = make_type(L"f32", FloatType::f32);
    f64 = make_type(L"f64", FloatType::f64);
    boolean = make_type(L"bool", BoolType {});
    string = make_type(L"string", SliceType { TypeRegistry::u32 });
    string_builder = make_type(L"string_builder", DynArray { TypeRegistry::u32 });
    cstring = make_type(L"cstring", ZeroTerminatedArray { TypeRegistry::u8 });
    character = make_type(L"char", TypeAlias { TypeRegistry::u32 });
    void_ = make_type(L"void", VoidType {});
    pointer = make_type(L"pointer", PointerType { void_ });
}

TypeRegistry &TypeRegistry::the()
{
    return s_registry;
}

pType TypeRegistry::generic_parameter(std::wstring name)
{
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&name](GenericParameter const descr) -> bool {
                        return descr.name == name;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"{}", name), GenericParameter { std::move(name) });
    return ret;
}

pType TypeRegistry::referencing(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](ReferenceType const descr) -> bool {
                        return descr.referencing == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"&{}", type->name), ReferenceType { type });
    return ret;
}

pType TypeRegistry::pointer_to(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](PointerType const descr) -> bool {
                        return descr.referencing == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"*{}", type->name), PointerType { type });
    return ret;
}

pType TypeRegistry::alias_for(pType type)
{
    assert(type);
    auto ret = make_type(std::format(L"AliasOf({})", type->name), TypeAlias { type });
    return ret;
}

pType TypeRegistry::slice_of(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](SliceType const descr) -> bool {
                        return descr.slice_of == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"[]{}", type->name), SliceType { type });
    return ret;
}

pType TypeRegistry::zero_terminated_array_of(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](ZeroTerminatedArray const &descr) -> bool {
                        return descr.array_of == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"[0]{}", type->name), ZeroTerminatedArray { type });
    return ret;
}

pType TypeRegistry::array_of(pType type, size_t size)
{
    assert(type);
    assert(size > 0);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type, &size](Array const &descr) -> bool {
                        return descr.array_of == type && descr.size == size;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"[{}]{}", size, type->name), Array { type, size });
    return ret;
}

pType TypeRegistry::dyn_array_of(pType type)
{
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](DynArray const &descr) -> bool {
                        return descr.array_of == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"[*]{}", type->name), DynArray { type });
    return ret;
}

pType TypeRegistry::optional_of(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](OptionalType const descr) -> bool {
                        return descr.type == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"?{}", type->name), OptionalType { type });
    return ret;
}

pType TypeRegistry::range_of(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](RangeType const descr) -> bool {
                        return descr.range_of == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"{}..", type->name), RangeType { type });
    return ret;
}

pType TypeRegistry::result_of(pType success, pType error)
{
    assert(success);
    assert(error);
    for (auto const &t : types) {
        if (std::visit(overloads {
                           [&success, &error](ResultType const descr) -> bool {
                               return descr.success == success && descr.error == error;
                           },
                           [](auto const &) -> bool {
                               return false;
                           } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"{}/{}", success->name, error->name), ResultType { success, error });
    return ret;
}

pType TypeRegistry::function_of(std::vector<pType> const &parameters, pType result)
{
    pType params_type_list = typelist_of(parameters);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&params_type_list, &result](FunctionType const &descr) -> bool {
                        return descr.parameters == params_type_list && descr.result == result;
                    },
                    [](auto const &x) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }

    auto ret = make_type(
        std::format(L"func{} {}", params_type_list->name, result->name),
        FunctionType { params_type_list, result });
    return ret;
}

pType TypeRegistry::typelist_of(std::vector<pType> const &typelist)
{
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&typelist](TypeList const &descr) -> bool {
                        return descr.types == typelist;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }

    auto n = std::format(
        L"({})",
        join_elements(
            typelist,
            std::wstring_view { L"," },
            [](pType const &t) -> std::wstring_view { return std::wstring_view { t->name }; }));
    auto ret = make_type(n, TypeList { typelist });
    return ret;
}

pType TypeRegistry::struct_of(StructType::Fields const &fields)
{
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&fields](StructType const &descr) -> bool {
                        if (fields.size() != descr.fields.size()) {
                            return false;
                        }
                        for (auto const &fld : std::ranges::views::zip(fields, descr.fields)) {
                            if (std::get<0>(fld).name != std::get<1>(fld).name || std::get<0>(fld).type != std::get<1>(fld).type) {
                                return false;
                            }
                        }
                        return true;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }

    auto n = std::format(
        L"{{{}}}",
        join_elements(
            fields,
            std::wstring_view { L"," },
            [](StructType::Field const &f) -> std::wstring { return std::format(L"{}: {}", f.name, f.type->to_string()); }));
    auto ret = make_type(n, StructType { fields });
    return ret;
}

pType TypeRegistry::type_of(pType type)
{
    assert(type);
    for (auto const &t : types) {
        if (std::visit(
                overloads {
                    [&type](TypeType const descr) -> bool {
                        return descr.type == type;
                    },
                    [](auto const &) -> bool {
                        return false;
                    } },
                t.description)) {
            return t.id;
        }
    }
    auto ret = make_type(std::format(L"meta({})", type->name), TypeType { type });
    return ret;
}
}

std::wostream &operator<<(std::wostream &os, Lia::pType const &type)
{
    os << type->to_string();
    return os;
}
