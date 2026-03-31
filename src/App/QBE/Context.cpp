
/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "App/Type.h"
#include <ranges>
#include <sstream>

#include <App/Parser.h>
#include <App/QBE/QBE.h>

namespace Lia::QBE {

ILValue QBEContext::add_string(std::wstring_view s)
{
    auto &file { program.files[current_file] };
    for (auto const &[ix, str] : std::ranges::views::enumerate(file.strings)) {
        if (str == s) {
            return ILValue::string(ix);
        }
    }
    file.strings.emplace_back(s);
    return ILValue::string(file.strings.size() - 1);
}

ILValue QBEContext::add_cstring(std::string_view s)
{
    auto &file { program.files[current_file] };
    for (auto const &[ix, str] : std::ranges::views::enumerate(file.cstrings)) {
        if (str == s) {
            return ILValue::cstring(ix);
        }
    }
    file.cstrings.emplace_back(s);
    return ILValue::cstring(file.cstrings.size() - 1);
}

ILValue QBEContext::add_enumeration(pType const &enum_type)
{
    auto &file { program.files[current_file] };
    auto  found { false };
    for (auto const &[ix, e] : std::ranges::views::enumerate(file.enumerations)) {
        if (e == enum_type) {
            return ILValue::global(std::format(L"enum$_{}", ix + 1), ILBaseType::L);
        }
    }
    if (!found) {
        file.enumerations.emplace_back(enum_type);
    }
    return ILValue::global(std::format(L"enum$_{}", file.enumerations.size()), ILBaseType::L);
}

ILType QBEContext::qbe_type(pType const &type)
{
    std::visit(
        overloads {
            [this](StructType const &strukt) {
                for (auto const &fld : strukt.fields) {
                    this->qbe_type(fld.type);
                }
            },
            [this](ReferenceType const &ref) {
                this->qbe_type(ref.referencing);
            },
            [this](OptionalType const &opt) {
                this->qbe_type(opt.type);
            },
            [this](ResultType const &result) {
                this->qbe_type(result.success);
                this->qbe_type(result.error);
            },
            [](auto const &) {
            },
        },
        type->description);
    auto  ret = Lia::QBE::qbe_type(type);
    auto &file { program.files[current_file] };
    auto  found { false };
    for (auto const &t : file.types) {
        if (type == t) {
            found = true;
            break;
        }
    }
    if (!found) {
        file.types.emplace_back(type);
    }
    return ret;
}

void QBEContext::add_operation(ILInstructionImpl impl)
{
    auto &file { program.files[current_file] };
    auto &function { file.functions[current_function] };
    if (!function.instructions.empty()
        && std::holds_alternative<RetDef>(function.instructions.back().impl)) {
        assert(std::holds_alternative<DbgLoc>(impl));
        return;
    }
    if (std::holds_alternative<LabelDef>(impl)) {
        auto const &label_def = std::get<LabelDef>(impl);
        if (function.labels.size() < label_def.label + 1) {
            function.labels.resize(label_def.label + 1);
        }
        function.labels[label_def.label] = function.instructions.size();
    }
    if (trace_on()) {
        std::wstringstream s;
        std::visit(
            [&s](auto const &i) -> void {
                s << i;
            },
            impl);
        trace(L"QBE {}", s.str());
    }
    function.instructions.emplace_back(std::move(impl));
}

std::optional<ILBinding> QBEContext::find(std::wstring_view name)
{
    return function().find(name);
}

ILFunction &QBEContext::add_function(std::wstring name, pType return_type)
{
    auto &file { program.files[current_file] };
    auto &function = file.functions.emplace_back(
        file,
        std::move(name),
        return_type,
        is_export);
    is_export = false;
    file.has_exports |= function.exported;
    file.has_main = function.name == L"main";
    next_var = 0;
    next_label = 0;
    current_function = function.id;
    return function;
}

ILBinding const &QBEContext::add(std::wstring_view name, pType const &type)
{
    return function().add(name, type);
}

ILBinding const &QBEContext::add_parameter(std::wstring_view name, pType const &type)
{
    return function().add_parameter(name, type);
}

ILTemporary const &QBEContext::add_temporary(pType const &type)
{
    return function().add_temporary(type, qbe_type(type));
}

ILFunction &QBEContext::function()
{
    return program.files[current_file].functions[current_function];
}

}
