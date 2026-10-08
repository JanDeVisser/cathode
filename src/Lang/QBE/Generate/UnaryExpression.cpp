#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, UnaryExpression const &impl, QBEContext &ctx)
{
    QBEOperand   operand { TRY_GENERATE(impl.operand, ctx) };
    QBEUnaryExpr expr { n, impl.op, operand };
    auto         var = qbe_operator(expr, ctx);
    return var;
}

}
