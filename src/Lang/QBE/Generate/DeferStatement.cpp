#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, DeferStatement const &impl, QBEContext &ctx)
{
    if (auto res = generate_qbe_node(impl.statement, ctx); !res) {
        return res;
    }
    return { };
}

}
