
/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include <rt/lia.h>

#include <Util/Align.h>
#include <Util/Logging.h>
#include <Util/Ptr.h>
#include <Util/Utf8.h>

#include <App/Keyword.h>

namespace Lia {

#define TypeKinds(S)       \
    S(VoidType)            \
    S(PointerType)         \
    S(NamespaceType)       \
    S(FunctionType)        \
    S(TypeList)            \
    S(GenericParameter)    \
    S(IntType)             \
    S(FloatType)           \
    S(BoolType)            \
    S(ReferenceType)       \
    S(SliceType)           \
    S(ZeroTerminatedArray) \
    S(Array)               \
    S(DynArray)            \
    S(RangeType)           \
    S(EnumType)            \
    S(TaggedUnionType)     \
    S(OptionalType)        \
    S(ResultType)          \
    S(StructType)          \
    S(TypeAlias)           \
    S(TypeType)

enum class TypeKind {
#undef S

#define S(K) K,
    TypeKinds(S)
#undef S
};

using pType = Ptr<struct Type, struct TypeRegistry>;
using pTypes = std::vector<pType>;

using Slice = slice_t;
using DynamicArray = dynarr_t;
using StaticArray = array_t;

#define BitWidths(S) \
    S(8)             \
    S(16)            \
    S(32)            \
    S(64)

#define FloatBitWidths(S) \
    S(32, float)          \
    S(64, double)

struct IntType {
    bool     is_signed;
    uint8_t  width_bits;
    uint64_t max_value;
    int64_t  min_value;

    static IntType u8;
    static IntType u16;
    static IntType u32;
    static IntType u64;
    static IntType i8;
    static IntType i16;
    static IntType i32;
    static IntType i64;

    std::wstring encode() const;
    std::wstring to_string() const;

    intptr_t size_of() const
    {
        return width_bits / 8;
    }

    intptr_t align_of() const
    {
        return width_bits / 8;
    }
};

struct FloatType {
    uint8_t width_bits;

    static FloatType f32;
    static FloatType f64;

    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return width_bits / 8;
    }

    intptr_t align_of() const
    {
        return width_bits / 8;
    }
};

struct BoolType {
    std::wstring to_string() const
    {
        return L"Bool";
    }

    intptr_t size_of() const
    {
        return 1;
    }

    intptr_t align_of() const
    {
        return 1;
    }

    std::wstring encode() const
    {
        return L"";
    }
};

struct VoidType {
    std::wstring to_string() const
    {
        return L"Void";
    }

    intptr_t size_of() const
    {
        return 0;
    }

    intptr_t align_of() const
    {
        return 0;
    }

    std::wstring encode() const
    {
        return L"";
    }
};

struct PointerType {
    pType        referencing;
    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return sizeof(void *);
    }

    intptr_t align_of() const
    {
        return alignof(void *);
    }
};

struct NamespaceType {
    std::wstring to_string() const
    {
        return L"Namespace";
    }

    intptr_t size_of() const
    {
        return 0;
    }

    intptr_t align_of() const
    {
        return 0;
    }

    std::wstring encode() const
    {
        return L"";
    }
};

struct FunctionType {
    pType parameters;
    pType result;

    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return sizeof(void *);
    }

    intptr_t align_of() const
    {
        return alignof(void *);
    }
};

struct TypeList {
    std::vector<pType> types;

    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    std::wstring encode() const;
};

struct GenericParameter {
    std::wstring name;

    std::wstring to_string() const
    {
        return std::format(L"<{}>", name);
    }

    intptr_t size_of() const
    {
        return 0;
    }

    intptr_t align_of() const
    {
        return 0;
    }

    std::wstring encode() const
    {
        return L"";
    }
};

struct ReferenceType {
    pType        referencing;
    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return sizeof(void *);
    }

    intptr_t align_of() const
    {
        return alignof(void *);
    }
};

struct SliceType {
    pType        slice_of;
    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return sizeof(Slice);
    }

    intptr_t align_of() const
    {
        return alignof(Slice);
    }
};

struct ZeroTerminatedArray {
    pType        array_of;
    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return sizeof(void *);
    }

    intptr_t align_of() const
    {
        return alignof(void *);
    }
};

struct Array {
    pType  array_of;
    size_t size;

    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    std::wstring encode() const;
};

struct DynArray {
    pType        array_of;
    std::wstring to_string() const;
    std::wstring encode() const;

    intptr_t size_of() const
    {
        return sizeof(DynamicArray);
    }

    intptr_t align_of() const
    {
        return alignof(DynamicArray);
    }
};

struct RangeType {
    pType range_of;

    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    std::wstring encode() const;
};

struct TypeAlias {
    pType        alias_of;
    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    std::wstring encode() const;
};

struct EnumType {
    struct Value {
        std::wstring label;
        int64_t      value;
    };
    using Values = std::vector<Value>;

    pType  underlying_type;
    Values values;

    std::wstring           to_string() const;
    intptr_t               size_of() const;
    intptr_t               align_of() const;
    bool                   is_valid(std::wstring_view label) const;
    std::optional<int64_t> value_for(std::wstring_view label) const;
    pType                  underlying() const;
    std::wstring           encode() const;
};

struct TaggedUnionType {
    struct Tag {
        int64_t value;
        pType   payload;
    };
    using Tags = std::vector<Tag>;

    pType tag_type;
    Tags  tags;

    std::wstring           to_string() const;
    intptr_t               size_of() const;
    intptr_t               align_of() const;
    bool                   is_valid(std::wstring_view label) const;
    std::optional<int64_t> value_for(std::wstring_view label) const;
    pType                  payload_for(std::wstring_view label) const;
    pType                  payload_for(int64_t tag) const;
    pType                  underlying() const;
    intptr_t               tag_offset() const;
    std::wstring           encode() const;
};

template<class S>
concept enumerated_type = std::is_same_v<S, EnumType> || std::is_same_v<S, TaggedUnionType>;

struct StructType {
    struct Field {
        std::wstring name;
        pType        type;
    };

    using Fields = std::vector<Field>;

    Fields       fields;
    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    std::wstring encode() const;

    auto field(std::wstring_view field_name)
    {
        return std::find_if(fields.begin(), fields.end(), [&field_name](StructType::Field const &fld) -> bool {
            return fld.name == field_name;
        });
    }

    size_t offset_of(std::wstring_view field_name) const;
};

struct OptionalType {
    pType type;

    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    std::wstring encode() const;
};

struct ResultType {
    pType success;
    pType error;

    std::wstring to_string() const;
    intptr_t     size_of() const;
    intptr_t     align_of() const;
    intptr_t     flag_offset() const;
    std::wstring encode() const;
};

struct TypeType {
    pType type;

    std::wstring to_string() const;
    std::wstring encode() const;
    intptr_t     size_of() const
    {
        return 0;
    }

    intptr_t align_of() const
    {
        return 0;
    }
};

using TypeDescription = std::variant<
#undef S
#define S(K) K,
    TypeKinds(S)
#undef S
        std::monostate>;

struct Type {
    pType                         id { nullptr };
    std::wstring                  name;
    TypeDescription               description;
    std::map<std::wstring, pType> arguments {};

    template<typename DescrType>
    Type(std::wstring n, DescrType descr)
        : name(std::move(n))
        , description(std::move(descr))
    {
    }

    bool                          is_a(TypeKind kind) const;
    TypeKind                      kind() const;
    std::wstring                  to_string() const;
    intptr_t                      size_of() const;
    intptr_t                      align_of() const;
    pType                         value_type() const;
    bool                          compatible(pType const &other) const;
    bool                          assignable_to(pType const &lhs) const;
    std::map<std::wstring, pType> infer_generic_arguments(pType const &param_type) const;
    std::wstring                  encode() const;
};

struct TypeRegistry {
    std::vector<Type> types;

    static TypeRegistry &the();

    size_t      size() const { return types.size(); }
    bool        empty() const { return types.empty(); }
    Type const &operator[](size_t ix) const { return types[ix]; }
    pType       generic_parameter(std::wstring name);
    pType       referencing(pType type);
    pType       pointer_to(pType type);
    pType       alias_for(pType type);
    pType       slice_of(pType type);
    pType       zero_terminated_array_of(pType type);
    pType       array_of(pType type, size_t size);
    pType       dyn_array_of(pType type);
    pType       optional_of(pType type);
    pType       range_of(pType type);
    pType       result_of(pType success, pType error);
    pType       function_of(std::vector<pType> const &parameters, pType result);
    pType       typelist_of(std::vector<pType> const &typelist);
    pType       struct_of(StructType::Fields const &fields);
    pType       type_of(pType type);

    static pType u8;
    static pType u16;
    static pType u32;
    static pType u64;
    static pType i8;
    static pType i16;
    static pType i32;
    static pType i64;
    static pType f32;
    static pType f64;
    static pType boolean;
    static pType string;
    static pType string_builder;
    static pType cstring;
    static pType character;
    static pType void_;
    static pType pointer;

private:
    TypeRegistry();
    static TypeRegistry s_registry;
};

template<class N>
N *get_if(pType const &type)
{
    assert(type);
    return std::get_if<N>(&type->description);
}

template<class N>
N const &get(pType const &type)
{
    assert(type);
    return std::get<N>(type->description);
}

template<class N>
bool is(pType const &type)
{
    assert(type);
    return std::holds_alternative<N>(type->description);
}

template<typename DescrType>
pType make_type(std::wstring n, DescrType descr)
{
    // std::wcout << "Registering type " << n << ' ' << descr.to_string() << std::endl;
    TypeRegistry::the().types.emplace_back(std::move(n), std::move(descr));
    pType ret { &TypeRegistry::the() };
    TypeRegistry::the().types.back().id = ret;
    return ret;
}

template<typename DescrType>
pType make_type(DescrType descr)
{
    return make_type(std::format(L"anon-{}", TypeRegistry::the().types.size()), std::move(descr));
}

template<typename T>
pType const &type_of()
{
    fatal("type_of<{}>() undefined", typeid(T).name());
}

#undef S
#define S(W)                                                                  \
    template<>                                                                \
    inline pType const &type_of<uint##W##_t>() { return TypeRegistry::u##W; } \
    template<>                                                                \
    inline pType const &type_of<int##W##_t>() { return TypeRegistry::i##W; }
BitWidths(S)
#undef S

#undef S
#define S(W, T) \
    template<>  \
    inline pType const &type_of<T>() { return TypeRegistry::f##W; }
    FloatBitWidths(S)
#undef S

        extern int __dummy__;

template<>
inline pType const &type_of<bool>() { return TypeRegistry::boolean; }
template<>
inline pType const &type_of<void *>() { return TypeRegistry::pointer; }
template<>
inline pType const &type_of<char *>() { return TypeRegistry::cstring; }

}

std::wostream &operator<<(std::wostream &os, Lia::pType const &type);

namespace std {

using namespace Lia;

template<>
struct formatter<pType, wchar_t> {
    bool with_type { false };

    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it == ctx.end() || *it == '}')
            return it;

        switch (*it) {
        case 't':
            with_type = true;
            break;
        default:
            throw std::format_error("Invalid format args for pType");
        }
        ++it;
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("Invalid format args for pType");
        }
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(pType const &type, FmtContext &ctx) const
    {
        std::wostringstream out;
        if (type != nullptr) {
            out << type;
        } else {
            out << "(null)";
        }
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

}
