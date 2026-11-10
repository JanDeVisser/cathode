#include <Lang/QBE/Generate/Generate.h>

#include "Lang/QBE/QBE.h"

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Abort const &impl, QBEContext &ctx)
{
    qbe_abort(impl.message, ctx);
    return QBEOperand { n, ILValue::null() };
}

}
