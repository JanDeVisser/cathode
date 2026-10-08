#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Module const &impl, QBEContext &ctx)
{
    auto &file = ctx.program.files.emplace_back(impl.name);
    file.id = ctx.program.files.size() - 1;
    ctx.current_file = file.id;
    ctx.current_function = std::numeric_limits<size_t>::max();
    if (auto res = generate_qbe_nodes(impl.statements, ctx); !res) {
        return res;
    }
    return QBEOperand { n, ILValue::null() };
}

template<>
GenResult generate_qbe_node(ASTNode const &n, ModuleProxy const &, QBEContext &)
{
    return QBEOperand { n, ILValue::null() };
}

}
