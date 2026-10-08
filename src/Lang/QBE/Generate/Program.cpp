#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Program const &impl, QBEContext &ctx)
{
    ctx.program.name = impl.name;
    auto &parser { *(n.repo) };
    for (auto const &[mod_name, mod] : parser.modules) {
        if (auto res = generate_qbe_node(mod, ctx); !res) {
            return res;
        }
    }
    return QBEOperand { n, ILValue::null() };
}

}
