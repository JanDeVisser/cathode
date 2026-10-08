#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Return const &impl, QBEContext &ctx)
{
    auto &file = ctx.program.files[ctx.current_file];
    auto &function = file.functions[ctx.current_function];
    if (impl.expression != nullptr) {
        if (function.return_type->size_of() > 8) {
            if (auto res = assign(QBEOperand { n, ILValue::return_value(ILBaseType::L) }, impl.expression, ctx); !res.has_value()) {
                return res;
            };
        } else {
            ctx += CopyDef { TRY_GENERATE(impl.expression, ctx).get_value(), ILValue::return_value(function.return_type) };
        }
    }
    if (!function.frames.empty()) {
        ctx += CopyDef { ILValue::integer(function.frames.front().block.id.value(), ILBaseType::L), ILValue::break_block() };
        ctx += JmpDef { function.frames.back().exit_label };
    } else {
        ctx += RetDef { basetype(ILType { function.return_type }) };
    }
    return QBEOperand { n, ILValue::null() };
}

}
