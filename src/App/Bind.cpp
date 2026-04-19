/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <expected>

#include <Util/StringUtil.h>
#include <Util/Utf8.h>

#include <App/Operator.h>
#include <App/Parser.h>
#include <App/SyntaxNode.h>
#include <App/Type.h>

#include <App/QBE/QBE.h>

namespace Lia {

using namespace std::literals;

BindResult bind(ASTNode node)
{
    assert(node != nullptr);
    Parser &parser = *(node.repo);
    assert(node->status >= ASTStatus::Normalized);
    if (node->status == ASTStatus::Bound) {
        return node->bound_type;
    } else if (node->status > ASTStatus::Bound) {
        return BindError { node->status };
    } else { // node->status < ASTStatus::Bound) {
        size_t stack_size = parser.namespaces.size();
        if (node->ns != nullptr) {
            parser.push_namespace(node);
        }
        parser.node_stack.emplace_back(node);
        auto retval = std::visit(
            [&node](auto impl) {
                auto id { node->id.id.value() };
                // We take the impl by value so we can update the copied
                // value in the bind but at the same time be able to create
                // new nodes and potentially move the nodes vector around.
                auto ret = impl.bind(node);

                // Only copy the impl back if the node is, you know,
                // still the node and hasn't been superceded in the bind
                if (id == node->id.id.value()) {
                    node->node = impl;
                }
                return ret;
            },
            node->node);
        parser.node_stack.pop_back();
        while (stack_size < parser.namespaces.size()) {
            parser.pop_namespace(node);
        }
        if (retval.has_value()) {
            auto const &type = retval.value();
            if (type == nullptr) {
                node->status = ASTStatus::Undetermined;
                parser.unbound_nodes.push_back(node);
                parser.unbound++;
                return BindError { ASTStatus::Undetermined };
            } else {
                node->bound_type = type;
                node->status = ASTStatus::Bound;
                return type;
            }
        } else {
            switch (retval.error()) {
            case ASTStatus::InternalError:
                dump(node, std::wcerr);
                assert("bind(): Internal error" == nullptr);
                break;
            case ASTStatus::Undetermined:
                parser.unbound_nodes.push_back(node);
                parser.unbound++;
                /* Fallthrough */
            default:
                node->status = retval.error();
                break;
            }
            return retval;
        }
    }
}
}
