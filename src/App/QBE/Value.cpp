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

}
