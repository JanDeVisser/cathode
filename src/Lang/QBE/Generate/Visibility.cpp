#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Extern const &impl, QBEContext &ctx)
{
    if (!impl.library.empty()) {
        ctx.program.libraries.emplace_back(impl.library);
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &, PublicDeclaration const &impl, QBEContext &ctx)
{
    ctx.is_export = true;
    auto ret = generate_qbe_node(impl.declaration, ctx);
    ctx.is_export = false;
    return ret;
}

}
