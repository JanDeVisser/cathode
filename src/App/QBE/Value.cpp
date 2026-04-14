/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <variant>

#include <App/Parser.h>
#include <App/QBE/QBE.h>

namespace Lia::QBE {

std::wostream &operator<<(std::wostream &os, ILValue const &value)
{
    std::visit(
        overloads {
            [](std::monostate const &) {
            },
            [&os](ILValue::Local const &local) {
                os << "%local_" << local.var;
            },
            [&os](ILValue::Global const &global) {
                os << "$" << global.name;
            },
            [&os](std::wstring const &literal) {
                os << literal;
            },
            [&os](ILValue::Temporary const &temporary) {
                os << "%temp_" << temporary.index;
            },
            [&os](ILValue::Variable const &var) {
                os << "%var_" << var.index;
            },
            [&os](ILValue::Parameter const &param) {
                os << "%param_" << param.index;
            },
            [&os](ILValue::ReturnValue const &) {
                os << "%ret$";
            },
            [&os, &value](double const &dbl) {
                std::visit(
                    overloads {
                        [&os](ILBaseType const &type) {
                            switch (type) {
                            case ILBaseType::D:
                                os << "d_";
                                break;
                            case ILBaseType::S:
                                os << "s_";
                                break;
                            default:
                                break;
                            }
                        },
                        [](auto const &) {
                            UNREACHABLE();
                        } },
                    value.type.inner);
                os << dbl;
            },
            [&os](int64_t const &i) {
                os << i;
            },
            [&os](std::vector<ILValue> const &seq) {
                auto first { true };
                for (auto const &v : seq) {
                    if (!first) {
                        os << ", ";
                    }
                    first = false;
                    os << v;
                }
            },
        },
        value.inner);
    return os;
}

void ILBinding::store(uint8_t *destination) const
{
    auto store_value = [](ILValue const &val, uint8_t *target_ptr) -> uint8_t * {
        assert(std::holds_alternative<int64_t>(val.inner));
        assert(std::holds_alternative<ILBaseType>(val.type.inner));
        auto i { std::get<int64_t>(val.inner) };
        auto t { std::get<ILBaseType>(val.type.inner) };

        uint8_t  p8;
        uint16_t p16;
        uint32_t p32;
        int64_t  p64;
        uint8_t *src_ptr;

        switch (t) {
        case ILBaseType::B:
        case ILBaseType::SB:
        case ILBaseType::UB:
            p8 = static_cast<uint8_t>(i);
            src_ptr = &p8;
            break;
        case ILBaseType::H:
        case ILBaseType::SH:
        case ILBaseType::UH:
            p16 = static_cast<uint16_t>(i);
            src_ptr = reinterpret_cast<uint8_t *>(&p16);
            break;
        case ILBaseType::W:
        case ILBaseType::SW:
        case ILBaseType::UW:
            p32 = static_cast<uint32_t>(i);
            src_ptr = reinterpret_cast<uint8_t *>(&p32);
            break;
        case ILBaseType::L:
            p64 = static_cast<int64_t>(i);
            src_ptr = reinterpret_cast<uint8_t *>(&p64);
            break;
        default:
            UNREACHABLE();
        }
        memset(target_ptr, 0, size_of(t));
        memcpy(target_ptr, src_ptr, size_of(t));
        return target_ptr + size_of(t);
    };

    std::visit(
        overloads {
            [&destination, &store_value](ILValue::ILValues const &values) {
                auto d { destination };
                for (auto const &v : values) {
                    d = store_value(v, d);
                }
            },
            [&destination, this, &store_value](auto const &) {
                store_value(value, destination);
            } },
        value.inner);
}

}
