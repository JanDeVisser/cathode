#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, IfStatement const &impl, QBEContext &ctx)
{
    LabelType cond_false { LabelType::End };
    if (impl.else_branch != nullptr) {
        cond_false = LabelType::Else;
    }
    auto condition_var = TRY_GENERATE(impl.condition, ctx);
    if (impl.condition->bound_type != TypeRegistry::boolean) {
        if (auto res = cast(condition_var, TypeRegistry::boolean, ctx); !res.has_value()) {
            return res;
        } else {
            condition_var = res.value();
        }
    }
    ctx.add_operation(
        JnzDef {
            condition_var.get_value(),
            QBELabel { LabelType::Top, n },
            QBELabel { cond_false, n },
        });
    ctx.add_operation(LabelDef { LabelType::Top, n });
    TRY_GENERATE(impl.if_branch, ctx);
    if (impl.else_branch != nullptr) {
        ctx.add_operation(JmpDef { LabelType::End, n });
        ctx.add_operation(LabelDef { LabelType::Else, n });
        TRY_GENERATE(impl.else_branch, ctx);
    }
    return QBEOperand { n, ILValue::null() };
}

}
