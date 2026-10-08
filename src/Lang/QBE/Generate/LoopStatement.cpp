#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, LoopStatement const &impl, QBEContext &ctx)
{
    ctx += LabelDef { LabelType::Top, n };
    TRY_GENERATE(impl.statement, ctx);
    ctx += LabelDef { LabelType::Bottom, n },
        JmpDef { LabelType::Top, n };
    return QBEOperand { n, ILValue::null() };
}

}
