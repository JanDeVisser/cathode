#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Break const &impl, QBEContext &ctx)
{
    ctx.add_operation(JmpDef { LabelType::End, impl.block });
    return QBEOperand { n, ILValue::null() };
}

}
