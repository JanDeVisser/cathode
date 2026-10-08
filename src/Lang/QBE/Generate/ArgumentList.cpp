#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, ArgumentList const &impl, QBEContext &ctx)
{
    ILValues values;
    int      align = 0;
    int      size = 0;
    for (auto const &expr : impl.arguments) {
        QBEOperand operand { TRY_GENERATE(expr, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
        auto const &type = values.back().type;
        if (size > 0) {
            size = alignat(size, align_of(type));
        }
        size += size_of(type);
        align = std::max(align, align_of(type));
    }
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
}

}
