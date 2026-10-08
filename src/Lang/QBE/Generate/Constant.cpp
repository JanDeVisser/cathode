#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Void const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, BoolConstant const &impl, QBEContext &)
{
    return QBEOperand { n, ILValue::integer(impl.value ? 1 : 0, ILBaseType::W) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Number const &impl, QBEContext &)
{
    auto const &int_type { get<IntType>(n->bound_type) };
    ILBaseType  t;
    int64_t     v;
    switch (int_type.width_bits) {
    case 8:
        t = (int_type.is_signed) ? ILBaseType::SB : ILBaseType::UB;
        v = (int_type.is_signed) ? get<int8_t>(impl) : get<uint8_t>(impl.value);
        break;
    case 16:
        t = (int_type.is_signed) ? ILBaseType::SH : ILBaseType::UH;
        v = (int_type.is_signed) ? get<int16_t>(impl) : get<uint16_t>(impl.value);
        break;
    case 32:
        t = (int_type.is_signed) ? ILBaseType::SW : ILBaseType::UW;
        v = (int_type.is_signed) ? get<int32_t>(impl.value) : get<uint32_t>(impl.value);
        break;
    case 64:
        t = ILBaseType::L;
        v = (int_type.is_signed) ? get<int64_t>(impl.value) : get<uint64_t>(impl.value);
        break;
    }
    return QBEOperand { n, ILValue::integer(v, t) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Decimal const &impl, QBEContext &)
{
    auto const &float_type { get<FloatType>(n->bound_type) };
    ILBaseType  t;
    double      dbl;
    switch (float_type.width_bits) {
    case 32:
        t = ILBaseType::S;
        dbl = static_cast<float>(impl.value);
        break;
    case 64:
        t = ILBaseType::D;
        dbl = impl.value;
        break;
    }
    return QBEOperand { n, ILValue::float_val(dbl, t) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, CString const &impl, QBEContext &ctx)
{
    return QBEOperand { n, ctx.add_cstring(static_cast<char const *>(impl.string.data())) };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, String const &impl, QBEContext &ctx)
{
    auto len_id { ++ctx.next_var };
    auto temp { ctx.add_temporary(TypeRegistry::string) };
    auto ret { ILValue::temporary(temp.index, TypeRegistry::string) };
    ctx.add_operation(
        StoreDef {
            ctx.add_string(impl.string),
            ret,
        });
    ctx.add_operation(
        ExprDef {
            ret,
            ILValue::integer(8, ILBaseType::L),
            ILOperation::Add,
            ILValue::pointer(len_id),
        });
    ctx.add_operation(
        StoreDef {
            ILValue::integer(impl.string.length(), ILBaseType::L),
            ILValue::pointer(len_id),
        });
    return QBEOperand { n, ret };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Dummy const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Nullptr const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

}
