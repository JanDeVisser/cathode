#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, VariableDeclaration const &impl, QBEContext &ctx)
{
    if (!is_constant(n)) {
        if (ctx.in_global_scope()) {
            return global_decl(n, impl.name, n->bound_type, impl.initializer, ctx);
        }
        return variable_decl(n, impl.name, n->bound_type, impl.initializer, ctx);
    }
    return QBEOperand { n, ILValue::null() };
}

}
