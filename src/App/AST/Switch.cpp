/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/QBE/QBE.h>

namespace Lia {

using namespace std::literals;

BindResult DefaultSwitchValue::bind(ASTNode const &) const
{
    return TypeRegistry::void_;
}

SwitchCase::SwitchCase(ASTNode case_value, ASTNode binding, ASTNode statement)
    : case_value(std::move(case_value))
    , binding(std::move(binding))
    , statement(std::move(statement))
{
}

BindResult SwitchCase::bind(ASTNode const &) const
{
    try_bind(case_value);
    try_bind(statement);
    return statement->bound_type;
}

SwitchStatement::SwitchStatement(Label label, ASTNode switch_value, ASTNodes switch_cases)
    : label(std::move(label))
    , switch_value(std::move(switch_value))
    , switch_cases(std::move(switch_cases))
{
}

BindResult SwitchStatement::bind(ASTNode const &n) const
{
    try_bind(switch_value);
    try_bind_nodes(switch_cases);

    auto   switch_type { switch_value->bound_type };
    auto  *tagged_union { get_if<TaggedUnionType>(switch_type) };
    size_t cases { 0 };
    auto   has_default { false };
    auto   default_in_list { false };
    for (auto const &c : switch_cases) {
        auto const &switch_case { get<SwitchCase>(c) };
        if (switch_case.binding != nullptr && tagged_union == nullptr) {
            return n.bind_error(L"Switch type `{}` does not allow a payload binding", switch_type->name);
        }
        pType       binding_type { nullptr };
        auto const &case_value { switch_case.case_value };
        auto        matches { std::visit(
            overloads {
                [&switch_type, &has_default, &default_in_list, &cases](ExpressionList const &impl) -> bool {
                    cases += impl.expressions.size();
                    return std::ranges::all_of(
                        impl.expressions,
                        [&switch_type, &has_default, &default_in_list](auto const &e) -> bool {
                            if (is<DefaultSwitchValue>(e)) {
                                default_in_list = true;
                                has_default = true;
                                e->bound_type = switch_type;
                                e->status = ASTStatus::Bound;
                            }
                            return e->bound_type == switch_type;
                        });
                },
                [&switch_type, &c, &has_default, &cases](DefaultSwitchValue const &) -> bool {
                    c->bound_type = switch_type;
                    c->status = ASTStatus::Bound;
                    has_default = true;
                    cases += 1;
                    return true;
                },
                [&switch_type, &case_value, &cases](auto const &) -> bool {
                    cases += 1;
                    return case_value->bound_type == switch_type;
                } },
            switch_case.case_value->node) };
        if (!matches) {
            return c.bind_error(
                L"Switch case value type `{}` different from switch value `{}`",
                case_value->bound_type->name, switch_type->name);
        }
        if (default_in_list) {
            return c.bind_error(
                L"Default switch case value `_` in a list makes no sense");
        }
    }
    if (has_default)
        --cases;
    if (auto cardi { cardinality(switch_type) }; cardi) {
        if (cases < cardi.value() && !has_default) {
            return n.bind_error(
                L"Not all switch values of type `{}` are handled",
                switch_type->name);
        }
        if (cases > cardi.value() || (has_default && cardi.value() == cases)) {
            return n.bind_error(
                L"There is a duplicate handler for a value of switch value type `{}`",
                switch_type->name);
        }
    } else {
        if (!has_default) {
            return n.bind_error(
                L"Switch statement with switch type `{}` should handle the default `_` case",
                switch_type->name);
        }
    }

    if (switch_cases.empty()) {
        return TypeRegistry::void_;
    }
    return switch_cases[0]->bound_type;
}

}
