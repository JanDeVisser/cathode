#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, SwitchStatement const &impl, QBEContext &ctx)
{
    auto switch_var = TRY_GENERATE(impl.switch_value, ctx);
    switch_var = TRY_DEREFERENCE(switch_var, ctx);
    ctx += LabelDef { LabelType::Top, n };
    ASTNode default_case { nullptr };
    for (auto const &c : impl.switch_cases) {
        auto switch_case { get<SwitchCase>(c) };

        ctx += LabelDef { LabelType::Begin, c };
        auto generate_case_value = [&switch_var, &ctx](ILValue const &value, ASTNode const &n, QBELabel const &value_ok, QBELabel const &value_not_ok) -> void {
            std::visit(
                overloads {
                    [&n, &switch_var, &ctx, &value, &value_ok, &value_not_ok](TagValue const &tag) -> void {
                        if (is<EnumType>(n->bound_type)) {
                            auto v = std::get<ILValues>(value.inner)[1];
                            auto match { ILValue::local(++ctx.next_var, ILBaseType::W) };
                            ctx += ExprDef { switch_var.get_value(), v, ILOperation::Equals, match },
                                JnzDef { match, value_ok, value_not_ok };
                            return { };
                        }
                        check_tagged_union(
                            tag.tag_value,
                            switch_var,
                            get<TaggedUnionType>(n->bound_type),
                            value_not_ok,
                            value_ok,
                            ctx);
                    },
                    [&value, &ctx, &value_ok, &value_not_ok, &switch_var](auto const &) -> void {
                        auto match { ILValue::local(++ctx.next_var, ILBaseType::W) };
                        ctx += ExprDef { switch_var.get_value(), value, ILOperation::Equals, match },
                            JnzDef { match, value_ok, value_not_ok };
                    } },
                n->node);
        };
        if (auto res = std::visit(
                overloads {
                    [&n, &generate_case_value, &c, &ctx, &switch_case](ExpressionList const &list) -> GenResult {
                        for (auto const &e : list.expressions) {
                            auto val = TRY_GENERATE(e, ctx);
                            val = TRY_DEREFERENCE(val, ctx);
                            generate_case_value(val.get_value(), e, QBELabel { LabelType::Top, c }, QBELabel { LabelType::Bottom, e });
                            ctx += LabelDef { LabelType::Bottom, e };
                        }
                        ctx += JmpDef { LabelType::End, c };
                        ctx += LabelDef { LabelType::Top, c };
                        TRY_GENERATE(switch_case.statement, ctx);
                        ctx += JmpDef { LabelType::End, n };
                        ctx += LabelDef { LabelType::End, c };
                        return { };
                    },
                    [&default_case, &switch_case](DefaultSwitchValue const &) -> GenResult {
                        default_case = switch_case.statement;
                        return { };
                    },
                    [&generate_case_value, &ctx, &c, &switch_case, &n](TagValue const &) -> GenResult {
                        auto case_var { TRY_GENERATE(switch_case.case_value, ctx) };
                        case_var = TRY_DEREFERENCE(case_var, ctx);
                        generate_case_value(case_var.get_value(), switch_case.case_value, QBELabel { LabelType::Top, c }, QBELabel { LabelType::End, c });
                        ctx += LabelDef { LabelType::Top, c };
                        TRY_GENERATE(switch_case.statement, ctx);
                        ctx += JmpDef { LabelType::End, n };
                        ctx += LabelDef { LabelType::End, c };
                        return { };
                    },
                    [&generate_case_value, &ctx, &c, &switch_case, &n](auto const &) -> GenResult {
                        auto case_var { TRY_GENERATE(switch_case.case_value, ctx) };
                        case_var = TRY_DEREFERENCE(case_var, ctx);
                        generate_case_value(case_var.get_value(), switch_case.case_value, QBELabel { LabelType::Top, c }, QBELabel { LabelType::End, c });
                        ctx += LabelDef { LabelType::Top, c };
                        TRY_GENERATE(switch_case.statement, ctx);
                        ctx += JmpDef { LabelType::End, n };
                        ctx += LabelDef { LabelType::End, c };
                        return { };
                    } },
                switch_case.case_value->node);
            !res) {
            return std::unexpected(res.error());
        }
    }

    if (default_case != nullptr) {
        TRY_GENERATE(default_case, ctx);
    }
    return QBEOperand { n, ILValue::null() };
}

}
