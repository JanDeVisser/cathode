/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

#include <rt/lia.h>

#include <Util/Logging.h>
#include <Util/StringUtil.h>

#include <App/Type.h>
#include <App/Value.h>

namespace Lia {

using namespace Util;

static std::vector<pType> atom_types {};
Arena                     Value::arena { Arena::BLOCK_SIZE };

pType Atom::type() const
{
    if (atom_types.empty()) {
        atom_types = {
            TypeRegistry::boolean,
#undef S
#define S(W)            \
    TypeRegistry::i##W, \
        TypeRegistry::u##W,
            BitWidths(S)
#undef S
                TypeRegistry::f32,
            TypeRegistry::f64,
            TypeRegistry::string,
            TypeRegistry::string_builder,
            TypeRegistry::void_,
            TypeRegistry::pointer
        };
    }
    return atom_types[payload.index()];
}

std::optional<Atom> Atom::coerce(pType const &to_type) const
{
    auto const &cur_type = type();
    if (cur_type == to_type) {
        return Atom { *this };
    }
    return std::visit(
        overloads {
            [&cur_type, &to_type](std::integral auto const &v) -> std::optional<Atom> {
                if (is<IntType>(to_type)) {
                    auto const &[is_signed, width_bits, max_value, min_value] = std::get<IntType>(to_type->description);
                    if (static_cast<uint64_t>(v) > max_value) {
                        return {};
                    }
                    if (static_cast<int64_t>(v) < min_value) {
                        return {};
                    }
#undef S
#define S(W)                                             \
    if (width_bits == W) {                               \
        if (is_signed) {                                 \
            return Atom { static_cast<int##W##_t>(v) };  \
        } else {                                         \
            return Atom { static_cast<uint##W##_t>(v) }; \
        }                                                \
    }
                    BitWidths(S)
#undef S
                }
                if (is<FloatType>(to_type)) {
                    auto const &[width_bits] = std::get<FloatType>(to_type->description);
#undef S
#define S(W, T)                            \
    if (width_bits == W) {                 \
        return Atom { static_cast<T>(v) }; \
    }
                    FloatBitWidths(S)
#undef S
                }
                if (is<PointerType>(to_type) && cur_type == TypeRegistry::u64) {
                    return Atom { reinterpret_cast<void *>(v) };
                }
                return {};
            },
            [&cur_type, &to_type](std::floating_point auto const &v) -> std::optional<Atom> {
                if (is<IntType>(to_type)) {
                    auto const &[is_signed, width_bits, max_value, min_value] = std::get<IntType>(to_type->description);
                    if (static_cast<uint64_t>(v) > max_value) {
                        return {};
                    }
                    if (static_cast<int64_t>(v) < min_value) {
                        return {};
                    }
#undef S
#define S(W)                                             \
    if (width_bits == W) {                               \
        if (is_signed) {                                 \
            return Atom { static_cast<int##W##_t>(v) };  \
        } else {                                         \
            return Atom { static_cast<uint##W##_t>(v) }; \
        }                                                \
    }
                    BitWidths(S)
#undef S
                }
                if (is<FloatType>(to_type)) {
                    auto const &[width_bits] = std::get<FloatType>(to_type->description);
#undef S
#define S(W, T)                            \
    if (width_bits == W) {                 \
        return Atom { static_cast<T>(v) }; \
    }
                    FloatBitWidths(S)
#undef S
                }
                return {};
            },
            [](auto const &) -> std::optional<Atom> {
                return {};
            } },
        payload);
}

std::wstring Atom::to_string() const
{
    return std::visit(
        overloads {
            [](std::integral auto const &value) -> std::wstring {
                return std::format(L"{}", value);
            },
            [](std::floating_point auto const &value) -> std::wstring {
                return std::format(L"{}", value);
            },
            [](bool const &value) -> std::wstring {
                return value ? L"true" : L"false";
            },
            [](Slice const &value) -> std::wstring {
                return std::format(L"Slice[{}]", value.size);
            },
            [](DynamicArray const &value) -> std::wstring {
                return std::format(L"DynamicArray[{},{}]", value.size, value.capacity);
            },
            [](StaticArray const &value) -> std::wstring {
                return std::format(L"StaticArray[{}]", value.size);
            },
            [](void *const &value) -> std::wstring {
                return std::format(L"void*[{}]", value);
            } },
        payload);
}

static bool is_zero(Atom const &atom);

#undef S
#define S(O) static Atom evaluate_##O(Atom const &lhs, Atom const &rhs);
BinOps(S)
#undef S

    static bool is_zero(Atom const &atom)
{
    return std::visit(
        overloads {
            [](std::integral auto const &value) -> bool {
                return value == 0;
            },
            [](std::floating_point auto const &value) -> bool {
                return value == 0.0;
            },
            [](bool const &value) -> bool {
                return !value;
            },
            [](Slice const &value) -> bool {
                return value.ptr == nullptr || value.size == 0;
            },
            [](DynamicArray const &value) -> bool {
                return value.ptr == nullptr || value.size == 0;
            },
            [](StaticArray const &value) -> bool {
                return value.ptr == nullptr;
            },
            [](void *const &value) -> bool {
                return value == nullptr;
            } },
        atom.payload);
}

template<typename Func>
static Atom evaluate_op(Atom const &lhs, Atom const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](std::integral auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { func(lhs_value, rhs_value) };
            },
            [&func](std::integral auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { func(lhs_value, rhs_value) };
            },
            [&func](std::floating_point auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { func(lhs_value, rhs_value) };
            },
            [&func](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { func(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> Atom {
                fatal("Operator only applicable to numbers, not to `{}` and `{}`",
                    typeid(decltype(lhs_value)).name(),
                    typeid(decltype(rhs_value)).name());
            } },
        lhs.payload, rhs.payload);
}

template<typename Func>
static Atom evaluate_relational_op(Atom const &lhs, Atom const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](std::integral auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [&func](std::integral auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [&func](std::floating_point auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [&func](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { static_cast<bool>(func(lhs_value, rhs_value)) };
            },
            [](auto lhs_value, auto rhs_value) -> Atom {
                fatal("Operator only applicable to numbers, not to `{}` and `{}`",
                    typeid(decltype(lhs_value)).name(),
                    typeid(decltype(rhs_value)).name());
            } },
        lhs.payload, rhs.payload);
}

template<typename Func>
static Atom evaluate_bitwise_op(Atom const &lhs, Atom const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](std::integral auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { func(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> Atom {
                fatal("Operator only applicable to integers");
            } },
        lhs.payload, rhs.payload);
}

template<typename Func>
static Atom evaluate_logical_op(Atom const &lhs, Atom const &rhs, Func const &func)
{
    return std::visit(
        overloads {
            [&func](bool lhs_value, bool rhs_value) -> Atom {
                return Atom { func(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> Atom {
                fatal("Operator only applicable to booleans");
            } },
        lhs.payload, rhs.payload);
}

Atom evaluate_AddressOf(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot take address of an atom");
}

Atom evaluate_Apply(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot Apply a value");
}

Atom evaluate_Call(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot call a value");
}

Atom evaluate_Cast(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot cast a value");
}

Atom evaluate_Length(Atom const &, Atom const &)
{
    fatal("Cannot take length of an Atom");
}

Atom evaluate_MemberAccess(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot access members of a value");
}

Atom evaluate_Range(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot build a range value");
}

Atom evaluate_Sequence(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot build a sequence value");
}

Atom evaluate_Subscript(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot subscript a value");
}

Atom evaluate_Add(Atom const &lhs, Atom const &rhs)
{
    return std::visit(
        overloads {
            [](std::integral auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { lhs_value + rhs_value };
            },
            [](std::integral auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { lhs_value + rhs_value };
            },
            [](std::floating_point auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { lhs_value + rhs_value };
            },
            [](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { lhs_value + rhs_value };
            },
            [](void *lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { static_cast<void *>(static_cast<uint8_t *>(lhs_value) + static_cast<intptr_t>(rhs_value)) };
            },
            [](auto lhs_value, auto rhs_value) -> Atom {
                fatal("Operator only applicable to numbers, not to `{}` and `{}`",
                    typeid(decltype(lhs_value)).name(),
                    typeid(decltype(rhs_value)).name());
            } },
        lhs.payload, rhs.payload);
}

Atom evaluate_Subtract(Atom const &lhs, Atom const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x - y; });
}

Atom evaluate_Multiply(Atom const &lhs, Atom const &rhs)
{
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x * y; });
}

Atom evaluate_Divide(Atom const &lhs, Atom const &rhs)
{
    if (is_zero(rhs)) {
        fatal("Division by zero");
    }
    return evaluate_op(lhs, rhs,
        [](auto x, auto y) { return x / y; });
}

Atom evaluate_Modulo(Atom const &lhs, Atom const &rhs)
{
    if (is_zero(rhs)) {
        fatal("Division by zero");
    }
    return std::visit(
        overloads {
            [](std::integral auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { lhs_value % rhs_value };
            },
            [](std::integral auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { fmod(lhs_value, rhs_value) };
            },
            [](std::floating_point auto lhs_value, std::integral auto rhs_value) -> Atom {
                return Atom { fmod(lhs_value, rhs_value) };
            },
            [](std::floating_point auto lhs_value, std::floating_point auto rhs_value) -> Atom {
                return Atom { fmod(lhs_value, rhs_value) };
            },
            [](auto lhs_value, auto rhs_value) -> Atom {
                fatal("Modulo operation only applicable to numbers");
            } },
        lhs.payload, rhs.payload);
}

Atom evaluate_Equals(Atom const &lhs, Atom const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x == y; });
}

Atom evaluate_NotEqual(Atom const &lhs, Atom const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x != y; });
}

Atom evaluate_Less(Atom const &lhs, Atom const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x < y; });
}

Atom evaluate_LessEqual(Atom const &lhs, Atom const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x <= y; });
}

Atom evaluate_Greater(Atom const &lhs, Atom const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x > y; });
}

Atom evaluate_GreaterEqual(Atom const &lhs, Atom const &rhs)
{
    return evaluate_relational_op(lhs, rhs,
        [](auto x, auto y) { return x >= y; });
}

Atom evaluate_BinaryAnd(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x & y; });
}

Atom evaluate_BinaryOr(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x | y; });
}

Atom evaluate_BinaryXor(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x ^ y; });
}

Atom evaluate_ShiftLeft(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x << y; });
}

Atom evaluate_ShiftRight(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, rhs,
        [](auto x, auto y) { return x >> y; });
}

Atom evaluate_LogicalAnd(Atom const &lhs, Atom const &rhs)
{
    return evaluate_logical_op(lhs, rhs,
        [](auto x, auto y) { return x && y; });
}

Atom evaluate_LogicalOr(Atom const &lhs, Atom const &rhs)
{
    return evaluate_logical_op(lhs, rhs,
        [](auto x, auto y) { return x || y; });
}

Atom evaluate_Idempotent(Atom const &lhs, Atom const &)
{
    return lhs;
}

Atom evaluate_Negate(Atom const &lhs, Atom const &)
{
    return evaluate_op(lhs, Atom { (uint32_t) 0 },
        [](auto x, auto) { return -x; });
}

Atom evaluate_BinaryInvert(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, Atom { (uint32_t) 0 },
        [](auto x, auto) { return ~x; });
}

Atom evaluate_LogicalInvert(Atom const &lhs, Atom const &rhs)
{
    return evaluate_bitwise_op(lhs, Atom { false },
        [](auto x, auto) { return !x; });
}

Atom evaluate_Sizeof(Atom const &lhs, Atom const &)
{
    return Atom { lhs.type()->size_of() };
}

Atom evaluate_Unwrap(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot unwrap a value");
}

Atom evaluate_UnwrapError(Atom const &lhs, Atom const &rhs)
{
    fatal("Cannot unwrap an error of a value");
}

Atom evaluate(Atom const &lhs, Operator op, Atom const &rhs)
{
    switch (op) {
#undef S
#define S(O)          \
    case Operator::O: \
        return evaluate_##O(lhs, rhs);
        BinOps(S)
#undef S
            default : UNREACHABLE();
    }
}

Atom evaluate(Operator op, Atom const &operand)
{
    switch (op) {
#undef S
#define S(O)          \
    case Operator::O: \
        return evaluate_##O(operand, Atom { false });
        BinOps(S)
#undef S
            default : UNREACHABLE();
    }
}

Value::Value(Value &other)
    : type(other.type)
{
    std::visit(
        overloads {
            [this](std::monostate const &) {
                this->payload = std::monostate {};
            },
            [this](Atom const &atom) {
                this->payload = atom;
            },
            [this](Values const &values) {
                this->payload = values;
            } },
        other.payload);
}

Value::Value(pType const &type)
    : type(type)
{
    switch (type->kind()) {
    case TypeKind::IntType: {
        auto int_type { std::get<IntType>(type->description) };
        switch (int_type.width_bits) {
        case 8:
            payload = Atom { (int_type.is_signed) ? static_cast<int8_t>(0) : static_cast<uint8_t>(0) };
            break;
        case 16:
            payload = Atom { (int_type.is_signed) ? static_cast<int16_t>(0) : static_cast<uint16_t>(0) };
            break;
        case 32:
            payload = Atom { (int_type.is_signed) ? static_cast<int32_t>(0) : static_cast<uint32_t>(0) };
            break;
        case 64:
            payload = Atom { (int_type.is_signed) ? static_cast<int64_t>(0) : static_cast<uint64_t>(0) };
            break;
        }
    } break;
    case TypeKind::FloatType: {
        auto flt_type { std::get<FloatType>(type->description) };
        switch (flt_type.width_bits) {
        case 32:
            payload = Atom { 0.0f };
            break;
        case 64:
            payload = Atom { 0.0 };
            break;
        }

    } break;
    case TypeKind::DynArray:
        payload = Atom { DynamicArray { nullptr, 0, 0 } };
        break;
    case TypeKind::SliceType:
        payload = Atom { Slice { nullptr, 0 } };
        break;
    case TypeKind::Array:
        payload = Atom { StaticArray { nullptr, 0 } };
        break;
    case TypeKind::VoidType:
        payload = std::monostate {};
        break;
    case TypeKind::StructType: {
        Value::Values fields;
        auto          struct_type { std::get<StructType>(type->description) };
        for (auto const &fd : struct_type.fields) {
            fields.emplace_back(fd.type);
        }
        payload = fields;
    } break;
    default:
        fatal(L"Need more types: {}", type->name);
        break;
    }
}

Value::Value(int8_t const val)
    : Value(TypeRegistry::i8, val)
{
}

Value::Value(uint8_t const val)
    : Value(TypeRegistry::u8, val)
{
}

Value::Value(int16_t const val)
    : Value(TypeRegistry::i16, val)
{
}

Value::Value(uint16_t const val)
    : Value(TypeRegistry::u16, val)
{
}

Value::Value(int32_t const val)
    : Value(TypeRegistry::i32, val)
{
}

Value::Value(uint32_t const val)
    : Value(TypeRegistry::u32, val)
{
}

Value::Value(int64_t const val)
    : Value(TypeRegistry::i64, val)
{
}

Value::Value(intptr_t const val)
    : Value(TypeRegistry::u64, val)
{
}

Value::Value(uint64_t const val)
    : Value(TypeRegistry::u64, val)
{
}

Value::Value(float const val)
    : Value(TypeRegistry::f32, val)
{
}

Value::Value(double const val)
    : Value(TypeRegistry::f64, val)
{
}

Value::Value(bool const val)
    : Value(TypeRegistry::boolean, val)
{
}

Value::Value(Slice const val)
    : Value(TypeRegistry::string, val)
{
}

Value::Value(void const *val)
    : Value(TypeRegistry::pointer, val)
{
}

Value::Value(Atom atom)
    : Value(atom.type(), atom)
{
}

Value::Value(std::nullptr_t)
    : Value(TypeRegistry::void_)
{
}

std::optional<Value> Value::coerce(pType const &to_type) const
{
    if (type == to_type) {
        return Value { *this };
    }
    if (type == TypeRegistry::string && to_type == TypeRegistry::cstring) {
        return Value { TypeRegistry::cstring, Atom { string_to_cstring(as<Slice>(*this)) } };
    }
    if (type == TypeRegistry::cstring && to_type == TypeRegistry::string) {
        return Value { TypeRegistry::string, Atom { cstring_to_string(static_cast<char const *>(as<void const *>(*this))) } };
    }
    if (is<DynArray>(type) && is<SliceType>(to_type) && std::get<DynArray>(type->description).array_of == std::get<SliceType>(type->description).slice_of) {
        auto const dyn_arr = as<DynamicArray>(*this);
        return Value { to_type, Slice { dyn_arr.ptr, dyn_arr.size } };
    }
    if (is<Array>(type) && is<SliceType>(to_type) && std::get<Array>(type->description).array_of == std::get<SliceType>(type->description).slice_of) {
        auto const arr = as<StaticArray>(*this);
        return Value { to_type, Slice { arr.ptr, arr.size } };
    }
    return std::visit(
        overloads {
            [this, &to_type](Atom const &atom) -> std::optional<Value> {
                if (auto const coerced_maybe = atom.coerce(to_type); coerced_maybe) {
                    return Value { *coerced_maybe };
                }
                return {};
            },
            [](auto const &) -> std::optional<Value> {
                fatal("Cannot apply operator to compound values");
            } },
        payload);
}

std::wstring Value::to_string() const
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> std::wstring {
                return L"``";
            },
            [](Atom const &atom) -> std::wstring {
                return atom.to_string();
            },
            [this](Value::Values const &values) -> std::wstring {
                int  ix = 0;
                auto strukt { std::get<StructType>(type->description) };
                return std::format(L"{{ {} }}",
                    join_elements(
                        values,
                        std::wstring_view { L"," },
                        [&ix, &strukt](Value const &v) -> std::wstring {
                            return std::format(
                                L"{}: {}",
                                strukt.fields[ix++].name,
                                v.to_string());
                        }));
            } },
        payload);
}

Value make_from_buffer(pType const &type, void *buffer)
{
    switch (type->kind()) {
    case TypeKind::VoidType: {
        return make_void();
    }
    case TypeKind::IntType: {
        uint64_t dummy { 0 };
        if (buffer == nullptr) {
            buffer = &dummy;
        }
        switch (auto const &d = std::get<IntType>(type->description); d.width_bits) {
#undef S
#define S(W) \
    case W:  \
        return (d.is_signed) ? Value(*static_cast<int##W##_t *>(buffer)) : Value(*static_cast<uint##W##_t *>(buffer));
            BitWidths(S) default : UNREACHABLE();
        }
    }
    case TypeKind::FloatType: {
        double dummy { 0 };
        if (buffer == nullptr) {
            buffer = &dummy;
        }
        switch (auto const &d = std::get<FloatType>(type->description); d.width_bits) {
#undef S
#define S(W, T) \
    case W:     \
        return Value(*static_cast<T *>(buffer));
            FloatBitWidths(S) default : UNREACHABLE();
        }
    }
    case TypeKind::BoolType: {
        bool dummy { false };
        if (buffer == nullptr) {
            buffer = &dummy;
        }
        return Value(*static_cast<bool *>(buffer));
    }
    case TypeKind::SliceType: {
        Slice dummy { nullptr, 0 };
        if (buffer == nullptr) {
            buffer = &dummy;
        }
        return make_value(type, *static_cast<Slice *>(buffer));
    }
    case TypeKind::DynArray: {
        DynamicArray dummy { nullptr, 0, 0 };
        if (buffer == nullptr) {
            buffer = &dummy;
        }
        return make_value(type, *static_cast<DynamicArray *>(buffer));
    }
    case TypeKind::Array: {
        StaticArray dummy { nullptr, 0 };
        if (buffer == nullptr) {
            buffer = &dummy;
        }
        return make_value(type, *static_cast<StaticArray *>(buffer));
    }
    default:
        fatal(L"make_from_buffer(`{}`) not implemented", type->to_string());
    }
}

void *address_of(Value &val)
{
    trace("address_of {}", reinterpret_cast<void *>(&val));
    auto ret = std::visit(
        overloads {
            [](std::monostate &) -> void * {
                fatal("Cannot convert empty value pointer");
            },
            [](Atom &atom) -> void * {
                return address_of(atom);
            },
            [](Value::Values &) -> void * {
                UNREACHABLE();
            } },
        val.payload);
    trace("address of payload {}", ret);
    return ret;
}

pType type_of(Value &val, std::vector<uint64_t> path)
{
    if (path.size() > 1) {
        std::vector<uint64_t> p;
        for (auto ix = 1; ix < path.size(); ++ix) {
            p.emplace_back(path[ix]);
        }
        return type_of(subvalue_of(val, path.front()), p);
    }
    return val.type;
}

void *address_of(Value &val, uint64_t field)
{
    trace("address_of({},{})", reinterpret_cast<void *>(&val), field);
    auto ret = std::visit(
        overloads {
            [](std::monostate &) -> void * {
                fatal("Cannot convert empty value pointer");
            },
            [](Atom &atom) -> void * {
                UNREACHABLE();
            },
            [field](Value::Values &values) -> void * {
                assert(field < values.size());
                return address_of(values[field]);
            } },
        val.payload);
    trace("address of payload {}", ret);
    return ret;
}

Value &subvalue_of(Value &val, uint64_t ix)
{
    return std::visit(
        overloads {
            [ix](Value::Values &sub_values) -> Value & {
                return sub_values[ix];
            },
            [](auto &) -> Value & {
                UNREACHABLE();
            } },
        val.payload);
}

void *address_of(Value &val, std::vector<uint64_t> const &path)
{
    trace("address_of({},{})",
        reinterpret_cast<void *>(&val),
        join_elements(path, ',', [](uint64_t i) { return std::format("{}", i); }));
    auto ret = std::visit(
        overloads {
            [](std::monostate &) -> void * {
                fatal("Cannot convert empty value pointer");
            },
            [&path, &val](Atom &atom) -> void * {
                if (path.empty()) {
                    return address_of(val);
                }
                UNREACHABLE();
            },
            [&path, &val](Value::Values &values) -> void * {
                if (path.empty()) {
                    return &val;
                }
                auto &v = val;
                for (auto ix = 0; ix + 1 < values.size(); ++ix) {
                    v = subvalue_of(v, path[ix]);
                }
                return address_of(v);
            } },
        val.payload);
    trace("address of payload {}", ret);
    return ret;
}

static Value evaluate_on_atoms(Value const &lhs, Operator op, Value const &rhs)
{
    return std::visit(
        overloads {
            [&lhs, op](Atom const &lhs_atom, Atom const &rhs_atom) -> Value {
                return Value { evaluate(lhs_atom, op, rhs_atom) };
            },
            [](auto, auto) -> Value {
                fatal("Cannot apply operator to compound values");
            } },
        lhs.payload, rhs.payload);
}

Value evaluate(Value const &lhs, Operator op, Value const &rhs)
{
    std::wstringstream s;
    s << lhs.to_string() << ' ' << Operator_name(op) << ' ' << rhs.to_string() << " = ";
    switch (op) {
    case Operator::Add: {
        if (lhs.type == TypeRegistry::string && rhs.type->kind() == TypeKind::IntType) {
            std::wstring ret {};
            auto         lhs_value = lhs.as_wstring();
            auto         rhs_value = as<uint64_t>(rhs);
            for (auto count = 0; count < rhs_value; ++count) {
                ret += lhs_value;
            }
            return make_value(ret);
        }
        if (lhs.type == TypeRegistry::string && rhs.type == TypeRegistry::string) {
            return make_value(lhs.as_wstring() + rhs.as_wstring());
        }
    } break;
    case Operator::Length: {
        switch (lhs.type->kind()) {
        case TypeKind::SliceType: {
            auto slice = as<Slice>(lhs);
            return make_value(slice.size);
        }
        case TypeKind::DynArray: {
            auto dyn_arr = as<DynamicArray>(lhs);
            return make_value(dyn_arr.size);
        }
        case TypeKind::Array: {
            auto arr = as<StaticArray>(lhs);
            return make_value(arr.size);
        }
        case TypeKind::ZeroTerminatedArray: {
            auto is_zero = [](char *ptr, size_t bytes) {
                for (auto ix = 0; ix < bytes; ++ix) {
                    if (ptr[ix] != 0)
                        return false;
                }
                return true;
            };
            int64_t len = 0;
            auto    ptr = (char const *) as<void const *>(lhs);
            auto    sz = lhs.type->size_of();
            while (true) {
                auto is_null { true };
                for (auto ix = 0; ix < lhs.type->size_of(); ++ix) {
                    if (*(ptr + sz * len + ix) != 0) {
                        is_null = false;
                        break;
                    }
                }
                if (is_null) {
                    break;
                }
                ++len;
            }
            return len;
        }
        default:
            break;
        }
    }
    case Operator::Sizeof:
        return Value { static_cast<int64_t>(lhs.type->size_of()) };
    default:
        break;
    }
    auto ret = evaluate_on_atoms(lhs, op, rhs);
    s << ' ' << ret.to_string();
    return ret;
}

Value evaluate(Operator op, Value const &operand)
{
    return evaluate(operand, op, Value { 0 });
}
}
