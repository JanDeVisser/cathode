#include "Lang/QBE/QBE.h"
#include <Lang/QBE/Generate/Generate.h>

namespace Lang::QBE {

template<>
GenResult generate_qbe_node(ASTNode const &n, Block const &impl, QBEContext &ctx)
{
    if (ctx.program.name.empty()) {
        ctx.program.name = std::format(L"anonymous-{}", n.id.value_or(0));
    }
    if (ctx.program.files.empty()) {
        auto &file = ctx.program.files.emplace_back(ctx.program.name);
        file.id = ctx.program.files.size() - 1;
        ctx.current_file = file.id;
        ctx.current_function = 0;
    }
    auto &file = ctx.program.files[ctx.current_file];
    if (ctx.current_function >= file.functions.size()) {
        ctx.add_function(file.name, n->bound_type);
    }
    auto    &function { file.functions[ctx.current_function] };
    ASTNodes defers { };
    function.frames.emplace_back(n, QBELabel { LabelType::Bottom, n });
    for (auto const &s : impl.statements) {
        if (std::holds_alternative<DeferStatement>(s->node)) {
            defers.push_back(s);
            function.frames.back().exit_label = QBELabel { LabelType::Begin, s };
        } else {
            if (auto res = generate_qbe_node(s, ctx); !res) {
                return res;
            }
        }
    }
    function.frames.pop_back();
    for (auto const &defer : defers | std::ranges::views::reverse) {
        if (auto res = generate_qbe_node(defer, ctx); !res) {
            return res;
        }
    }
    ctx += LabelDef { LabelType::Bottom, n };
    if (!function.frames.empty()) {
        auto break_is_zero_temp { function.add_temporary(TypeRegistry::i64, ILBaseType::L) };
        auto break_is_zero { ILValue::temporary(break_is_zero_temp.index, ILBaseType::L) };
        auto break_is_self_temp { function.add_temporary(TypeRegistry::i64, ILBaseType::L) };
        auto break_is_self { ILValue::temporary(break_is_self_temp.index, ILBaseType::L) };
        auto break_is_either_temp { function.add_temporary(TypeRegistry::i64, ILBaseType::L) };
        auto break_is_either { ILValue::temporary(break_is_either_temp.index, ILBaseType::L) };
        ctx += ExprDef { ILValue::break_block(), ILValue::integer(0, ILBaseType::L), ILOperation::Equals, break_is_zero },
            ExprDef { ILValue::break_block(), ILValue::integer(n.id.value(), ILBaseType::L), ILOperation::Equals, break_is_self },
            ExprDef { break_is_self, break_is_zero, ILOperation::Or, break_is_either },
            JnzDef { break_is_zero, QBELabel { LabelType::End, n }, QBELabel { LabelType::Bottom, function.frames.back().block } };
    }
    return { };
}

}
