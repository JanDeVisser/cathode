#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, ExpressionList const &impl, QBEContext &ctx)
{
    ILValues values;
    for (auto const &expr : impl.expressions) {
        QBEOperand operand { TRY_GENERATE(expr, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
    }
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
}

}
