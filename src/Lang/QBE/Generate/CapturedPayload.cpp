#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &, CapturedPayload const &impl, QBEContext &ctx)
{
    return generate_qbe_node(impl.tag_value, ctx);
}

}
