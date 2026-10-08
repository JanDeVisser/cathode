#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Identifier const &impl, QBEContext &ctx)
{
    assert(n->bound_type != nullptr);
    assert(n->bound_type.repo != nullptr);
    auto t = ctx.qbe_type(n->bound_type);
    if (is<ReferenceType>(n->bound_type)) {
        for (auto const &binding : ctx.function().parameters) {
            if (binding.name == impl.identifier && !binding.var_index) {
                return QBEOperand { n, ILValue::parameter(binding.param_index, t) };
            }
        }
    }
    for (auto const &binding : ctx.function().variables) {
        if (binding.name == impl.identifier) {
            return QBEOperand { n, ILValue::variable(binding.index, t) };
        }
    }
    for (auto const &binding : ctx.file().globals) {
        if (binding.name == impl.identifier) {
            return QBEOperand { n, ILValue::global(binding.name, t) };
        }
    }
    fatal(L"Could not find variable `{}`", impl.identifier);
}

}
