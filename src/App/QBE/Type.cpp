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

ILType::ILType(pType const &type)
    : inner(ILAggregate { type })
{
    if (qbe_first_class_type(type)) {
        inner = qbe_type_code(type);
    } else {
        inner = ILAggregate { type };
    }
}

bool operator==(ILType const &type, ILBaseType other)
{
    return std::holds_alternative<ILBaseType>(type.inner) && std::get<ILBaseType>(type.inner) == other;
}

bool operator==(ILBaseType const &type, ILType const &other)
{
    return std::holds_alternative<ILBaseType>(other.inner) && std::get<ILBaseType>(other.inner) == type;
}

bool operator!=(ILType const &type, ILBaseType other)
{
    return !std::holds_alternative<ILBaseType>(type.inner) || std::get<ILBaseType>(type.inner) != other;
}

bool operator!=(ILBaseType const &type, ILType const &other)
{
    return !std::holds_alternative<ILBaseType>(other.inner) || std::get<ILBaseType>(other.inner) != type;
}

bool qbe_first_class_type(pType const &type)
{
    auto const &t = type->value_type();
    return std::visit(
        overloads {
            [](IntType const &) -> bool {
                return true;
            },
            [](FloatType const &) -> bool {
                return true;
            },
            [](BoolType const &) -> bool {
                return true;
            },
            [](EnumType const &e) -> bool {
                return qbe_first_class_type(e.underlying_type);
            },
            [](PointerType const &) -> bool {
                return true;
            },
            [](ZeroTerminatedArray const &) -> bool {
                return true;
            },
            [](auto const &) -> bool {
                return false;
            },
        },
        t->description);
}

ILBaseType qbe_type_code(IntType const &type)
{
    switch (type.width_bits) {
    case 8:
        return ILBaseType::B;
    case 16:
        return ILBaseType::H;
    case 32:
        return ILBaseType::W;
    case 64:
        return ILBaseType::L;
    default:
        UNREACHABLE();
    }
}

ILBaseType qbe_type_code(FloatType const &type)
{
    switch (type.width_bits) {
    case 32:
        return ILBaseType::S;
    case 64:
        return ILBaseType::D;
    default:
        UNREACHABLE();
    }
}

ILBaseType qbe_type_code(ZeroTerminatedArray const &)
{
    return ILBaseType::L;
}

ILBaseType qbe_type_code(BoolType const &)
{
    return ILBaseType::W;
}

ILBaseType qbe_type_code(StructType const &)
{
    return ILBaseType::L;
}

ILBaseType qbe_type_code(TypeList const &)
{
    return ILBaseType::L;
}

ILBaseType qbe_type_code(auto const &type)
{
    warning(L"Assuming qbe_type_code(`{}') is `l`", demangle<decltype(type)>());
    return ILBaseType::L;
}

ILBaseType qbe_type_code(pType const &type)
{
    return std::visit(
        [](auto const &descr) -> ILBaseType {
            return qbe_type_code(descr);
        },
        type->description);
}

ILBaseType qbe_load_code(pType const &type)
{
    return std::visit(
        overloads {
            [](IntType const &int_type) -> ILBaseType {
                switch (int_type.width_bits) {
                case 8:
                    return (int_type.is_signed) ? ILBaseType::SB : ILBaseType::UB;
                case 16:
                    return (int_type.is_signed) ? ILBaseType::SH : ILBaseType::UH;
                case 32:
                    return (int_type.is_signed) ? ILBaseType::SW : ILBaseType::UW;
                case 64:
                    return ILBaseType::L;
                default:
                    UNREACHABLE();
                }
            },
            [](BoolType const &) -> ILBaseType {
                return ILBaseType::W;
            },
            [](FloatType const &float_type) -> ILBaseType {
                return qbe_type_code(float_type);
            },
            [](EnumType const &enum_type) -> ILBaseType {
                return qbe_load_code(enum_type.underlying_type);
            },
            [](auto const &) -> ILBaseType {
                return ILBaseType::L;
            } },
        type->description);
}

int is_integer(ILBaseType type)
{
    return type <= ILBaseType::L;
}

int is_float(ILBaseType type)
{
    return type >= ILBaseType::S;
}

ILType::ILAggregate::ILAggregate(pType const &type)
    : name(type_ref(type))
    , size_of(type->size_of())
    , align_of(type->align_of())
    , layout(flatten_type(type))
{
}

ILBaseType basetype(ILType const &type)
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> ILBaseType {
                UNREACHABLE();
            },
            [](ILBaseType const &base_type) -> ILBaseType {
                return static_cast<ILBaseType>(static_cast<uint8_t>(base_type) & 0xFC);
            },
            [](auto const &) -> ILBaseType {
                return ILBaseType::L;
            } },
        type.inner);
}

void flatten_type(pType const &type, ILLayout &layout);

void flatten_type(BoolType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::W);
}

void flatten_type(EnumType const &enum_type, ILLayout &layout)
{
    flatten_type(enum_type.underlying_type, layout);
}

void flatten_type(TaggedUnionType const &tagged_union, ILLayout &layout)
{
    pType largest { TypeRegistry::void_ };
    std::ranges::for_each(
        tagged_union.tags,
        [&largest](auto const &tag) {
            if (tag.payload->size_of() > largest->size_of()) {
                largest = tag.payload;
            }
        });
    flatten_type(largest, layout);
    flatten_type(tagged_union.tag_type, layout);
}

void flatten_type(FloatType const &flt_type, ILLayout &layout)
{
    layout.emplace_back(qbe_type_code(flt_type));
}

void flatten_type(IntType const &int_type, ILLayout &layout)
{
    layout.emplace_back(qbe_type_code(int_type));
}

void flatten_type(PointerType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::L);
}

void flatten_type(ReferenceType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::L);
}

void flatten_type(SliceType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::L);
    layout.emplace_back(ILBaseType::L);
}

void flatten_type(StructType const &strukt, ILLayout &layout)
{
    for (auto const &field : strukt.fields) {
        flatten_type(field.type, layout);
    }
}

void flatten_type(TypeType const &type_type, ILLayout &layout)
{
    flatten_type(type_type.type, layout);
}

void flatten_type(OptionalType const &opt, ILLayout &layout)
{
    flatten_type(opt.type, layout);
    layout.emplace_back(ILBaseType::W);
}

void flatten_type(ResultType const &result, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::W);
    flatten_type((result.success->size_of() > result.error->size_of()) ? result.success : result.error);
}

void flatten_type(VoidType const &, ILLayout &)
{
}

void flatten_type(TypeList const &type_list, ILLayout &layout)
{
    for (auto const &t : type_list.types) {
        flatten_type(t, layout);
    }
}

void flatten_type(ZeroTerminatedArray const &, ILLayout &)
{
}

void flatten_type(auto const &descr, std::vector<ILBaseType> &)
{
    NYI("flatten_type for {}", typeid(descr).name());
}

void flatten_type(pType const &type, std::vector<ILBaseType> &components)
{
    std::vector<ILBaseType> ret { };
    std::visit(
        [&components](auto const &descr) {
            return flatten_type(descr, components);
        },
        type->description);
}

ILLayout flatten_type(pType const &type)
{
    ILLayout ret;
    flatten_type(type, ret);
    return ret;
}

ILType qbe_type(pType const &type)
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> ILType {
                return { };
            },
            [](VoidType const &) -> ILType {
                return { };
            },
            [](BoolType const &) -> ILType {
                return ILBaseType::W;
            },
            [](FloatType const &flt_type) -> ILType {
                switch (flt_type.width_bits) {
                case 32:
                    return ILBaseType::S;
                case 64:
                    return ILBaseType::D;
                default:
                    UNREACHABLE();
                }
            },
            [](IntType const &int_type) -> ILType {
                switch (int_type.width_bits) {
                case 8:
                    return (int_type.is_signed) ? ILBaseType::SB : ILBaseType::UB;
                case 16:
                    return (int_type.is_signed) ? ILBaseType::SH : ILBaseType::UH;
                case 32:
                    return ILBaseType::W;
                case 64:
                    return ILBaseType::L;
                default:
                    UNREACHABLE();
                }
            },
            [](ReferenceType const &) -> ILType {
                return ILBaseType::L;
            },
            [](ZeroTerminatedArray const &) -> ILType {
                return ILBaseType::L;
            },
            [](EnumType const &enum_type) -> ILType {
                return qbe_type(enum_type.underlying_type);
            },
            [&type](auto const &) -> ILType {
                return ILType { type };
            } },
        type->description);
}

ILBaseType must_extend(ILType const &type)
{
    return std::visit(
        overloads {
            [](ILBaseType const &base_type) -> ILBaseType {
                switch (base_type) {
                case ILBaseType::B:
                case ILBaseType::SB:
                    return ILBaseType::SB;
                case ILBaseType::UB:
                    return ILBaseType::UB;
                case ILBaseType::H:
                case ILBaseType::SH:
                    return ILBaseType::SH;
                case ILBaseType::UH:
                    return ILBaseType::UH;
                case ILBaseType::W:
                case ILBaseType::SW:
                case ILBaseType::UW:
                    return ILBaseType::W;
                case ILBaseType::L:
                    return ILBaseType::L;
                default:
                    UNREACHABLE();
                }
            },
            [](auto const &) -> ILBaseType {
                UNREACHABLE();
            } },
        type.inner);
}

ILBaseType targettype(ILType const &type)
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> ILBaseType {
                UNREACHABLE();
            },
            [](ILBaseType const &base_type) -> ILBaseType {
                switch (base_type) {
                case ILBaseType::L:
                case ILBaseType::S:
                case ILBaseType::D:
                case ILBaseType::W:
                    return base_type;
                default:
                    return ILBaseType::W;
                }
            },
            [](auto const &) -> ILBaseType {
                return ILBaseType::L;
            } },
        type.inner);
}

int align_of(ILBaseType const &type)
{
    switch (type) {
#undef S
#define S(T, Code, Str, Align, Size) \
    case ILBaseType::T:              \
        return Align;                \
        break;
        ILBASETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

int align_of(ILType::ILAggregate const &type)
{
    return type.align_of;
}

int align_of(std::monostate const &)
{
    return 0;
}

int size_of(ILBaseType const &type)
{
    switch (type) {
#undef S
#define S(T, Code, Str, Align, Size) \
    case ILBaseType::T:              \
        return Size;                 \
        break;
        ILBASETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

int size_of(ILType::ILAggregate const &type)
{
    return type.size_of;
}

int size_of(std::monostate const &)
{
    return 0;
}

int align_of(ILType const &type)
{
    return std::visit([](auto const &t) -> int {
        return align_of(t);
    },
        type.inner);
}

int size_of(ILType const &type)
{
    return std::visit([](auto const &t) -> int {
        return size_of(t);
    },
        type.inner);
}

std::wostream &operator<<(std::wostream &os, ILBaseType const &type)
{
    switch (type) {
#undef S
#define S(T, Code, Str, Align, Size) \
    case ILBaseType::T:              \
        os << #Str;                  \
        break;
        ILBASETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, ILType::ILAggregate const &aggregate)
{
    return os << aggregate.name;
}

std::wostream &operator<<(std::wostream &os, ILType const &type)
{
    std::visit(
        [&os](auto const &i) {
            os << i;
        },
        type.inner);
    return os;
}

}
