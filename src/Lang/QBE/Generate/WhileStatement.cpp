#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, WhileStatement const &impl, QBEContext &ctx)
{
    auto condition_var = TRY_GENERATE(impl.condition, ctx);
    if (impl.condition->bound_type != TypeRegistry::boolean) {
        if (auto res = cast(condition_var, TypeRegistry::boolean, ctx); !res.has_value()) {
            return res;
        } else {
            condition_var = res.value();
        }
    }
    ctx += JnzDef { condition_var.get_value(), QBELabel { LabelType::Top, n }, QBELabel { LabelType::End, n } },
        LabelDef { LabelType::Top, n };
    TRY_GENERATE(impl.statement, ctx);
    ctx += JmpDef { LabelType::Begin, n };
    return QBEOperand { n, ILValue::null() };
}

}
