#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, TagValue const &impl, QBEContext &ctx)
{
    if (impl.operand != nullptr) {
        auto ret = TRY_GENERATE(impl.operand, ctx);
        ret.node = n;
        return ret;
    }
    ILValues values;
    if (impl.payload == nullptr) {
        values.emplace_back(ILValue { });
    } else {
        QBEOperand operand { TRY_GENERATE(impl.payload, ctx) };
        values.emplace_back(TRY_DEREFERENCE(operand, ctx).get_value());
    }
    auto underlying_type = std::visit(
        overloads {
            [](enumerated_type auto const &enoom) -> ILBaseType {
                return qbe_type_code(enoom.underlying());
            },
            [](auto const &) -> ILBaseType {
                UNREACHABLE();
            } },
        n->bound_type->description);
    values.emplace_back(ILValue::integer(impl.tag_value, underlying_type));
    return QBEOperand { n, ILValue::sequence(values, n->bound_type) };
}

}
