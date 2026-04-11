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

std::wostream &operator<<(std::wostream &os, ILFunction const &function)
{
    if (function.exported) {
        os << "export ";
    }
    os << "function";
    if (function.return_type != TypeRegistry::void_) {
        os << ' ' << qbe_type(function.return_type);
    }
    os << " $" << function.name << '(';
    auto first = true;
    for (auto const &[ix, param] : std::ranges::views::enumerate(function.parameters)) {
        if (!first) {
            os << ", ";
        }
        first = false;
        os << param.il_type << " %param_" << ix;
    }
    os << R"() {
@start
)";
    if (function.return_type->size_of() > 8) {
        os << AllocDef {
            8,
            static_cast<size_t>(function.return_type->size_of()),
            ILValue::return_value(ILBaseType::L),
        } << '\n';
    }
    for (auto const &binding : function.variables) {
        os << AllocDef {
            (binding.type->size_of() < 8) ? 4ul : 8ul,
            static_cast<size_t>(binding.type->size_of()),
            ILValue::variable(binding.index, binding.type),
        } << '\n';
    }
    for (auto const &temp : function.temps) {
        os << AllocDef {
            (temp.type->size_of() < 8) ? 4ul : 8ul,
            static_cast<size_t>(temp.type->size_of()),
            ILValue::temporary(temp.index, temp.type),
        } << '\n';
    }
    for (auto const &instruction : function.instructions) {
        os << instruction;
    }
    if (!std::holds_alternative<RetDef>(function.instructions.back().impl)) {
        if (function.return_type != TypeRegistry::void_) {
            os << ILInstruction { RetDef { ILValue::integer(0, ILBaseType::L) } };
        } else {
            os << ILInstruction { RetDef { } };
        }
    }
    os << R"(}

)";
    return os;
}

ILFunction::ILFunction(ILFile &file, std::wstring name, pType return_type, bool exported)
    : file_id(file.id)
    , name(std::move(name))
    , return_type(return_type)
    , exported(exported)
    , id(file.functions.size())
{
}

ILBinding const &ILFunction::add(std::wstring_view name, pType type)
{
    auto  value { ILValue::variable(variables.size(), type) };
    auto &ret { variables.emplace_back(std::wstring { name }, std::move(type), variables.size(), std::move(value)) };
    return ret;
}

ILParameter const &ILFunction::add_parameter(std::wstring_view name, pType const &type)
{
    std::optional<int> var_index { };
    if (!is<ReferenceType>(type)) {
        var_index = variables.size();
    }
    return parameters.emplace_back(std::wstring { name }, type, parameters.size(), var_index, qbe_type(type));
}

ILTemporary const &ILFunction::add_temporary(pType const &type, ILType il_type)
{
    return temps.emplace_back(temps.size(), type, std::move(il_type));
}

}
