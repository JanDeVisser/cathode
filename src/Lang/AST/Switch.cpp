/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <Lang/Operator.h>
#include <Lang/Parser.h>
#include <Lang/SyntaxNode.h>
#include <Lang/Type.h>

#include <Lang/QBE/QBE.h>

namespace Lang {

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

ASTNode SwitchCase::normalized(ASTNode const &n) const
{
    n->init_namespace();
    auto normalized_case_value = std::visit(
        overloads {
            [this](ExpressionList const &list) -> ASTNode {
                ASTNodes values;
                for (auto const &e : list.expressions) {
                    if (auto const *id { get_if<Identifier>(e) }; id != nullptr) {
                        if (id->identifier == L"_"sv) {
                            values.emplace_back(normalize(make_node<DefaultSwitchValue>(e)));
                            continue;
                        }
                    }
                    values.emplace_back(normalize(e));
                }
                return make_node<ExpressionList>(case_value, values);
            },
            [this](Identifier const &id) -> ASTNode {
                if (id.identifier == L"_"sv) {
                    return normalize(make_node<DefaultSwitchValue>(case_value));
                }
                return normalize(case_value);
            },
            [this](auto const &) -> ASTNode {
                return normalize(case_value);
            } },
        case_value->node);
    return make_node<SwitchCase>(n, normalized_case_value, normalize(binding), normalize(statement));
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

ASTNode SwitchStatement::normalized(ASTNode const &n) const
{
    return make_node<SwitchStatement>(n, label, normalize(switch_value), normalize(switch_cases));
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
