#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Alias const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Enum const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Struct const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

}
