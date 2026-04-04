/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <cstdint>

#include <Util/Align.h>
#include <Util/Logging.h>
#include <Util/Resolve.h>

#include <App/Parser.h>
#include <App/QBE/QBE.h>

extern "C" {

struct ARM64Trampoline {
    Lia::void_t fnc { nullptr };
    union {
        uint64_t x[8] { 0, 0, 0, 0, 0, 0, 0, 0 };
        uint8_t  x_flat[64];
    };
    double   d[8] { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    uint64_t int_return_value { 0 };
    double   double_return_value { 0.0 };
};

struct X86_64Trampoline {
    Lia::void_t fnc { nullptr };
    uint64_t    int_regs[6] { 0, 0, 0, 0, 0, 0 };
    double      float_regs[8] { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    uint64_t    int_return_value[2] { 0, 0 };
    double      double_return_value { 0.0 };
};

#if IS_ARM64

#define NATIVE_CALL native_call_arm64
#define TRAMPOLINE ARM64Trampoline

#endif

#if IS_X86_64

#define NATIVE_CALL native_call_x86_64
#define TRAMPOLINE X86_64Trampoline

#endif

int trampoline(void *);
}

namespace Lia::QBE {

using namespace Util;
using namespace Lia;

template<typename T>
T as(void *ptr, intptr_t offset = 0)
{
    T ret;
    memcpy(&ret, reinterpret_cast<uint8_t *>(ptr) + offset, sizeof(T));
    return ret;
}

template<typename T>
void set(void *ptr, T val)
{
    memcpy(reinterpret_cast<uint8_t *>(ptr), &val, sizeof(T));
}

#if IS_ARM64

bool native_call_arm64(std::string_view name, void *params, std::vector<ILType> const &types, void *return_value, ILType const &return_type)
{
    if (types.size() > 8) {
        fatal("Can't do native calls with more than 8 parameters");
    }
    ARM64Trampoline t;
    if (auto const res = Resolver::get_resolver().resolve(std::string { name }); !res.has_value() || res.value() == nullptr) {
        fatal("Function `{}` not found", name);
    } else {
        t.fnc = res.value();
    }

    size_t int_reg = 0;
    size_t float_reg = 0;

    auto allocate_value = [&t, &int_reg, &float_reg](ILBaseType bt, uint8_t *from, intptr_t offset) {
        if (bt <= ILBaseType::L && int_reg >= 8) {
            fatal("Can only pass arguments in registers now");
        } else if (float_reg >= 8) {
            fatal("Can only pass arguments in registers now");
        }
        trace(L"Allocating register from 0x{:016x}", reinterpret_cast<intptr_t>(from + offset));
        switch (bt) {
        case ILBaseType::B:
        case ILBaseType::SB:
            t.x[int_reg++] = as<int8_t>(from, offset);
            break;
        case ILBaseType::UB:
            t.x[int_reg++] = as<uint8_t>(from, offset);
            break;
        case ILBaseType::H:
        case ILBaseType::SH:
            t.x[int_reg++] = as<int16_t>(from, offset);
            break;
        case ILBaseType::UH:
            t.x[int_reg++] = as<uint16_t>(from, offset);
            break;
        case ILBaseType::W:
        case ILBaseType::SW:
            t.x[int_reg++] = as<int32_t>(from, offset);
            break;
        case ILBaseType::UW:
            t.x[int_reg++] = as<uint32_t>(from, offset);
            break;
        case ILBaseType::L:
            t.x[int_reg++] = as<int64_t>(from, offset);
            break;
        case ILBaseType::S:
            t.d[float_reg++] = as<float>(from, offset);
            break;
        case ILBaseType::D:
            t.d[float_reg++] = as<float>(from, offset);
            break;
        default:
            break;
        }
    };

    intptr_t offset { 0 };
    auto    *p { static_cast<uint8_t *>(params) };
    for (size_t ix = 0; ix < types.size(); ++ix) {
        auto const &type = types[ix];

        std::visit(
            overloads {
                [](std::monostate const &) {
                },
                [&allocate_value, &offset, &p](ILBaseType const &bt) {
                    allocate_value(bt, p, offset);
                    offset += alignat(size_of(bt), 8);
                },
                [&t, &allocate_value, &offset, &p, &int_reg, &type](ILType::ILAggregate const &aggregate) {
                    int   align { 0 };
                    auto *mem = p + offset;
                    offset += alignat(sizeof(void *), 8);

                    assert(int_reg + (alignat(size_of(type), 8) / 8) < 8);
                    memcpy(t.x_flat + (8 * int_reg), mem, size_of(type));
                    int_reg += alignat(size_of(type), 8) / 8;
                } },
            type.inner);
    }

    trace("Trampoline:");
    trace("  Function: {}", reinterpret_cast<intptr_t>(t.fnc));
    trace("  Integer Registers:");
    for (auto ix = 0; ix < 6; ++ix) {
        trace("    {}: 0x{:016x}", ix, t.x[ix]);
    }

    if (auto const trampoline_result = trampoline(&t); trampoline_result) {
        fatal("Error executing `{}`. Trampoline returned {}", name, trampoline_result);
    }
    trace("  Integer result: {}", t.int_return_value);

    std::visit(
        overloads {
            [&return_value, &t](ILBaseType const &bt) {
                switch (bt) {
                case ILBaseType::V:
                    break;
                case ILBaseType::B:
                case ILBaseType::SB:
                    set(return_value, static_cast<int8_t>(t.int_return_value));
                    break;
                case ILBaseType::UB:
                    set(return_value, static_cast<uint8_t>(t.int_return_value));
                    break;
                case ILBaseType::H:
                case ILBaseType::SH:
                    set(return_value, static_cast<int16_t>(t.int_return_value));
                    break;
                case ILBaseType::UH:
                    set(return_value, static_cast<uint16_t>(t.int_return_value));
                    break;
                case ILBaseType::W:
                case ILBaseType::SW:
                    set(return_value, static_cast<int32_t>(t.int_return_value));
                    break;
                case ILBaseType::UW:
                    set(return_value, static_cast<uint32_t>(t.int_return_value));
                    break;
                case ILBaseType::L:
                    set(return_value, t.int_return_value);
                    break;
                case ILBaseType::S:
                    set(return_value, static_cast<float>(t.double_return_value));
                    break;
                case ILBaseType::D:
                    set(return_value, t.double_return_value);
                    break;
                default:
                    break;
                }
            },
            [](auto const &) {
                NYI("Structured return type");
            } },
        return_type.inner);
    return true;
    // auto *r { static_cast<uint64_t *>(return_value) };
    // auto  sz = size_of(return_type);
    // if (sz > 0 && sz <= 8) {
    //     *r = t.int_return_value;
    // } else if (sz > 16) {
    //     fatal("Cannot return big structures yet");
    // }

    //     // Stage A - Initialization
    //     // This stage is performed exactly once, before processing of the arguments
    //     // commences.
    //
    //     // A.1 The Next General-purpose Register Number (NGRN) is set to zero.
    //     size_t int_reg = 0;
    //     // A.2 The Next SIMD and Floating-point Register Number (NSRN) is set to
    //     // zero.
    //     size_t reg = 0;
    //     // A.3 The Next Scalable Predicate Register Number (NPRN) is set to zero.
    //     size_t nprn = 0;
    //     // A.4 The next stacked argument address (NSAA) is set to the current
    //     // stack-pointer value (SP).
    //     size_t nsaa = 0;
    //
    //     intptr_t offset { 0 };
    //     for (size_t ix = 0; ix < types.size(); ++ix) {
    //         auto const &type = types[ix];
    //
    //         // Stage B – Pre-padding and extension of arguments
    //         // For each argument in the list the first matching rule from the
    //         // following list is applied. If no rule matches the argument is used
    //         // unmodified.
    //
    //         // B.1 If the argument type is a Pure Scalable Type, no change is made
    //         // at this stage.
    //
    //         // B.2 If the argument type is a Composite Type whose size cannot be
    //         // statically determined by both the caller and the callee, the
    //         // argument is copied to memory and the argument is replaced by a
    //         // pointer to the copy. (There are no such types in C/C++ but they
    //         // exist in other languages or in language extensions).
    //
    //         // B.3 If the argument type is an HFA or an HVA, then the argument is
    //         // used unmodified.
    //         // TODO
    //
    //         // B.4 If the argument type is a Composite Type that is larger than 16
    //         // bytes, then the argument is copied to memory allocated by the caller
    //         // and the argument is replaced by a pointer to the copy.
    //         // TODO
    //
    //         // B.5 If the argument type is a Composite Type then the size of the
    //         // argument is rounded up to the nearest multiple of 8 bytes.
    //
    //         // B.6 If the argument is an alignment adjusted type its value is passed
    //         // as a copy of the actual value. The copy will have an alignment
    //         // defined as follows:
    //         //     • For a Fundamental Data Type, the alignment is the natural
    //         //       alignment of that type, after any promotions.
    //         //     • For a Composite Type, the alignment of the copy will have
    //         //       8-byte alignment if its natural alignment is ≤ 8 and 16-byte
    //         //       alignment if its natural alignment is ≥ 16.
    //         // The alignment of the copy is used for applying marshaling rules.
    //
    //         // Stage C – Assignment of arguments to registers and stack
    //         // For each argument in the list the following rules are applied in
    //         // turn until the argument has been allocated. When an argument is
    //         // assigned to a register any unused bits in the register have
    //         // unspecified value. When an argument is assigned to a stack slot any
    //         // unused padding bytes have unspecified value.
    //
    //         // C.1 If the argument is a Half-, Single-, Double- or Quad- precision
    //         // Floating-point or short vector type and the NSRN is less than 8, then
    //         // the argument is allocated to the least significant bits of register
    //         // v[NSRN]. The NSRN is incremented by one. The argument has now been
    //         // allocated.
    //         switch (type) {
    //         case TypeKind::FloatType: {
    //             if (reg < 8) {
    //                 if (et == TypeRegistry::f32) {
    //                     t.d[reg] = as<float>(params, offset);
    //                 } else {
    //                     t.d[reg] = as<double>(params, offset);
    //                 }
    //                 ++reg;
    //             }
    //         } break;
    //
    //             // C.2 If the argument is an HFA or an HVA and there are sufficient
    //             // unallocated SIMD and Floating-point registers (NSRN + number of
    //             // members ≤ 8), then the argument is allocated to SIMD and
    //             // Floating-point registers (with one register per member of the HFA or
    //             // HVA). The NSRN is incremented by the number of registers used. The
    //             // argument has now been allocated.
    //             // TODO
    //
    //             // C.3 If the argument is an HFA or an HVA then the NSRN is set to 8 and
    //             // the size of the argument is rounded up to the nearest multiple of 8
    //             // bytes.
    //             // TODO
    //
    //             // C.4 If the argument is an HFA, an HVA, a Quad-precision
    //             // Floating-point or short vector type then the NSAA is rounded up to
    //             // the next multiple of 8 if its natural alignment is ≤ 8 or the next
    //             // multiple of 16 if its natural alignment is ≥ 16.
    //             // TODO
    //
    //             // C.5 If the argument is a Half- or Single- precision Floating Point
    //             // type, then the size of the argument is set to 8 bytes. The effect is
    //             // as if the argument had been copied to the least significant bits of a
    //             // 64-bit register and the remaining bits filled with unspecified
    //             // values.
    //             // Not supported
    //
    //             // C.6 If the argument is an HFA, an HVA, a Half-, Single-, Double- or
    //             // Quad- precision Floating-point or short vector type, then the
    //             // argument is copied to memory at the adjusted NSAA. The NSAA is
    //             // incremented by the size of the argument. The argument has now been
    //             // allocated.
    //             // TODO
    //
    //             // C.7 If the argument is a Pure Scalable Type that consists of NV
    //             // Scalable Vector Types and NP Scalable Predicate Types, if the
    //             // argument is named, if NSRN+NV ≤ 8, and if NPRN+NP ≤ 4, then the
    //             // Scalable Vector Types are allocated in order to
    //             // z[NSRN]...z[NSRN+NV-1] inclusive and the Scalable Predicate Types are
    //             // allocated in order to p[NPRN]...p[NPRN+NP-1] inclusive. The NSRN is
    //             // incremented by NV and the NPRN is incremented by NP. The argument has
    //             // now been allocated.
    //             // TODO
    //
    //             // C.8 If the argument is a Pure Scalable Type that has not been
    //             // allocated by the rules above, then the argument is copied to memory
    //             // allocated by the caller and the argument is replaced by a pointer to
    //             // the copy (as for B.4 above). The argument is then allocated according
    //             // to the rules below.
    //             // TODO
    //
    //             // C.9 If the argument is an Integral or Pointer Type, the size of the
    //             // argument is less than or equal to 8 bytes and the NGRN is less than
    //             // 8, the argument is copied to the least significant bits in x[NGRN].
    //             // The NGRN is incremented by one. The argument has now been allocated.
    //         case TypeKind::IntType:
    //             if (int_reg < 6) {
    //                 auto int_type = std::get<IntType>(et->description);
    //                 switch (int_type.width_bits) {
    //                 case 8:
    //                     t.x[int_reg] = (int_type.is_signed) ? as<int8_t>(params, offset) : as<uint8_t>(params, offset);
    //                     break;
    //                 case 16:
    //                     t.x[int_reg] = (int_type.is_signed) ? as<int16_t>(params, offset) : as<uint16_t>(params, offset);
    //                     break;
    //                 case 32:
    //                     t.x[int_reg] = (int_type.is_signed) ? as<int32_t>(params, offset) : as<uint32_t>(params, offset);
    //                     break;
    //                 case 64:
    //                     t.x[int_reg] = (int_type.is_signed) ? as<int64_t>(params, offset) : as<uint64_t>(params, offset);
    //                     break;
    //                 }
    //                 // #undef S
    //                 // #define S(W)                            \
    //                 //     if (et == TypeRegistry::i##W) {     \
    //                 //         t.int_regs[int_reg] = as<int##W##_t>(v);  \
    //                 //     }                                   \
    //                 //     if (et == TypeRegistry::u##W) {     \
    //                 //         t.int_regs[int_reg] = as<uint##W##_t>(v); \
    //                 //     }
    //                 //                 BitWidths(S)
    //                 // #undef S
    //                 ++int_reg;
    //             }
    //             break;
    //
    //         case TypeKind::BoolType:
    //             if (int_reg < 8) {
    //                 t.x[int_reg] = as<bool>(params, offset);
    //                 ++int_reg;
    //             }
    //             break;
    //
    //         case TypeKind::PointerType:
    //         case TypeKind::ReferenceType:
    //         case TypeKind::ZeroTerminatedArray:
    //             if (int_reg < 8) {
    //                 t.x[int_reg] = reinterpret_cast<intptr_t>(as<void *>(params, offset));
    //                 ++int_reg;
    //             }
    //             break;
    //
    //         case TypeKind::SliceType:
    //             if (int_reg < 7) {
    //                 auto const &[ptr, size] = as<Slice>(params, offset);
    //                 t.x[int_reg++] = reinterpret_cast<intptr_t>(ptr);
    //                 t.x[int_reg++] = size;
    //             }
    //             break;
    //
    //         case TypeKind::DynArray:
    //             if (int_reg < 6) {
    //                 t.x[int_reg++] = reinterpret_cast<intptr_t>(as<DynamicArray>(params, offset).ptr);
    //                 t.x[int_reg++] = as<DynamicArray>(params, offset).size;
    //                 t.x[int_reg++] = as<DynamicArray>(params, offset).capacity;
    //             }
    //             break;
    //
    //         case TypeKind::Array:
    //             if (int_reg < 8) {
    //                 t.x[int_reg++] = reinterpret_cast<intptr_t>(as<StaticArray>(params, offset).ptr);
    //                 t.x[int_reg++] = as<StaticArray>(params, offset).size;
    //             }
    //             break;
    //
    //         default:
    //             NYI("more value types");
    //
    //             // C.10 If the argument has an alignment of 16 then the NGRN is rounded
    //             // up to the next even number.
    //
    //             // C.11 If the argument is an Integral Type, the size of the argument
    //             // is equal to 16 and the NGRN is less than 7, the argument is copied to
    //             // x[NGRN] and x[NGRN+1]. x[NGRN] shall contain the lower addressed
    //             // double-word of the memory representation of the argument. The NGRN
    //             // is incremented by two. The argument has now been allocated.
    //
    //             // C.12 If the argument is a Composite Type and the size in double-words
    //             // of the argument is not more than 8 minus NGRN, then the argument is
    //             // copied into consecutive general-purpose registers, starting at
    //             // x[NGRN]. The argument is passed as though it had been loaded into the
    //             // registers from a double-word-aligned address with an appropriate
    //             // sequence of LDR instructions loading consecutive registers from
    //             // memory (the contents of any unused parts of the registers are
    //             // unspecified by this standard). The NGRN is incremented by the number
    //             // of registers used. The argument has now been allocated.
    //
    //             // if (type_kind(et) == TK_AGGREGATE) {
    //             //     size_t size_in_double_words = alignat(typeid_sizeof(et->type_id), 8) / 8;
    //             //     if (size_in_double_words <= (8 - int_reg)) {
    //             //         uint64_t buffer[size_in_double_words];
    //             //         size_t   sz = datum_binary_image(v, buffer);
    //             //         assert(sz <= size_in_double_words * 8);
    //             //         for (size_t ix2 = 0; ix2 < size_in_double_words; ++ix2) {
    //             //             t.int_regs[int_reg] = buffer[ix2];
    //             //             ++int_reg;
    //             //         }
    //             //         continue;
    //             //     }
    //             // }
    //
    //             // C.13 The NGRN is set to 8.
    //
    //             // C.14 The NSAA is rounded up to the larger of 8 or the Natural
    //             // Alignment of the argument’s type.
    //
    //             // C.15 If the argument is a composite type then the argument is copied
    //             // to memory at the adjusted NSAA. The NSAA is incremented by the size
    //             // of the argument. The argument has now been allocated.
    //
    //             // C.16 If the size of the argument is less than 8 bytes then the size
    //             // of the argument is set to 8 bytes. The effect is as if the argument
    //             // was copied to the least significant bits of a 64-bit register and the
    //             // remaining bits filled with unspecified values.
    //
    //             // C.17 The argument is copied to memory at the adjusted NSAA. The NSAA
    //             // is incremented by the size of the argument. The argument has now been
    //             // allocated.
    //         }
    //         offset += alignat(type->size_of(), 8);
    //     }
    //
    //     trace("Trampoline:");
    //     trace("  Function: {}", reinterpret_cast<intptr_t>(t.fnc));
    //     trace("  Integer Registers:");
    //     for (auto ix = 0; ix < 8; ++ix) {
    //         trace("    {}: 0x{:016x}", ix, t.x[ix]);
    //     }
    //
    //     if (auto const trampoline_result = trampoline(&t); trampoline_result) {
    //         fatal("Error executing `{}`. Trampoline returned {}", name, trampoline_result);
    //     }
    //     trace("  Integer result: {}", t.int_return_value);
    //
    //     switch (return_type->kind()) {
    //     case TypeKind::IntType: {
    // #undef S
    // #define S(W)                                                             \
    //     if (return_type == TypeRegistry::i##W) {                             \
    //         set(return_value, static_cast<int##W##_t>(t.int_return_value));  \
    //         return true;                                                     \
    //     }                                                                    \
    //     if (return_type == TypeRegistry::u##W) {                             \
    //         set(return_value, static_cast<uint##W##_t>(t.int_return_value)); \
    //         return true;                                                     \
    //     }
    //         BitWidths(S)
    // #undef S
    //             UNREACHABLE();
    //     } break;
    //     case TypeKind::FloatType: {
    //         if (return_type == TypeRegistry::f32) {
    //             set(return_value, static_cast<float>(t.double_return_value));
    //             return true;
    //         }
    //         if (return_type == TypeRegistry::f64) {
    //             set(return_value, t.double_return_value);
    //             return true;
    //         }
    //         UNREACHABLE();
    //     } break;
    //     case TypeKind::BoolType:
    //         set(return_value, static_cast<bool>(t.int_return_value));
    //         return true;
    //     case TypeKind::PointerType:
    //     case TypeKind::ReferenceType:
    //         set(return_value, reinterpret_cast<void *>(static_cast<intptr_t>(t.int_return_value)));
    //         return true;
    //     case TypeKind::VoidType:
    //         return true;
    //     default:
    //         UNREACHABLE();
    //     }
}

#endif

bool native_call_x86_64(std::string_view name, uint8_t *params, std::vector<ILType> const &types, uint8_t *return_value, ILType const &return_type)
{
    if (types.size() > 6) {
        fatal("Can't do native calls with more than 6 parameters");
    }
    X86_64Trampoline t;
    if (auto const res = Resolver::get_resolver().resolve(std::string { name }); !res.has_value() || res.value() == nullptr) {
        fatal("Function `{}` not found", name);
    } else {
        t.fnc = res.value();
    }

    size_t int_reg = 0;
    size_t float_reg = 0;

    auto allocate_value = [&t, &int_reg, &float_reg](ILBaseType bt, uint8_t *from, intptr_t offset) {
        if (bt <= ILBaseType::L && int_reg >= 6) {
            fatal("Can only pass arguments in registers now");
        } else if (float_reg >= 8) {
            fatal("Can only pass arguments in registers now");
        }
        trace(L"Allocating register from 0x{:016x}", reinterpret_cast<intptr_t>(from + offset));
        switch (bt) {
        case ILBaseType::B:
        case ILBaseType::SB:
            t.int_regs[int_reg++] = as<int8_t>(from, offset);
            break;
        case ILBaseType::UB:
            t.int_regs[int_reg++] = as<uint8_t>(from, offset);
            break;
        case ILBaseType::H:
        case ILBaseType::SH:
            t.int_regs[int_reg++] = as<int16_t>(from, offset);
            break;
        case ILBaseType::UH:
            t.int_regs[int_reg++] = as<uint16_t>(from, offset);
            break;
        case ILBaseType::W:
        case ILBaseType::SW:
            t.int_regs[int_reg++] = as<int32_t>(from, offset);
            break;
        case ILBaseType::UW:
            t.int_regs[int_reg++] = as<uint32_t>(from, offset);
            break;
        case ILBaseType::L:
            t.int_regs[int_reg++] = as<int64_t>(from, offset);
            break;
        case ILBaseType::S:
            t.float_regs[float_reg++] = as<float>(from, offset);
            break;
        case ILBaseType::D:
            t.float_regs[float_reg++] = as<float>(from, offset);
            break;
        default:
            break;
        }
    };

    intptr_t offset { 0 };
    for (size_t ix = 0; ix < types.size(); ++ix) {
        auto const &type = types[ix];

        std::visit(
            overloads {
                [](std::monostate const &) {
                },
                [&allocate_value, &offset, &params](ILBaseType const &bt) {
                    allocate_value(bt, params, offset);
                    offset += alignat(size_of(bt), 8);
                },
                [&allocate_value, &offset, &params](ILType::ILAggregate const &aggregate) {
                    int   align { 0 };
                    auto *mem = params + offset;
                    offset += alignat(sizeof(void *), 8);

                    auto fld_offset = 0;
                    for (auto comp : aggregate.layout) {
                        fld_offset = alignat(fld_offset, align_of(comp));
                        allocate_value(comp, mem, fld_offset);
                        align = std::max(align, align_of(comp));
                        fld_offset += size_of(comp);
                    }
                } },
            type.inner);
    }

    trace("Trampoline:");
    trace("  Function: {}", reinterpret_cast<intptr_t>(t.fnc));
    trace("  Integer Registers:");
    for (auto ix = 0; ix < 6; ++ix) {
        trace("    {}: 0x{:016x}", ix, t.int_regs[ix]);
    }

    if (auto const trampoline_result = trampoline(&t); trampoline_result) {
        fatal("Error executing `{}`. Trampoline returned {}", name, trampoline_result);
    }
    trace("  Integer result: {}", t.int_return_value[0]);

    auto sz = size_of(return_type);
    if (sz > 0 && sz <= 16) {
        memcpy(return_value, t.int_return_value, std::min(sz, 8));
        if (sz > 8) {
            memcpy(return_value, t.int_return_value + 1, std::min(sz - 8, 8));
        }
    } else if (sz > 16) {
        fatal("Cannot return big structures yet");
    }
    return true;
}

bool native_call(std::string_view name, uint8_t *params, std::vector<ILType> const &types, uint8_t *return_value, ILType const &return_type)
{
    return NATIVE_CALL(name, params, types, return_value, return_type);
}

}
