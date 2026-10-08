#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &, Comptime const &impl, QBEContext &ctx)
{
    return generate_qbe_node(impl.statements, ctx);
}

}
