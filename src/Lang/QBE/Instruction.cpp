/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "Util/Logging.h"
#include <sstream>

#include <Lang/Parser.h>
#include <Lang/QBE/QBE.h>
#include <Lang/Type.h>
#include <variant>

namespace Lang::QBE {

std::wostream &operator<<(std::wostream &os, ILOperation const &op)
{
    switch (op) {
#undef S
#define S(Op, Str, LangOp) \
    case ILOperation::Op: \
        os << #Str;       \
        break;
        ILOPERATIONS(S)
#undef S
    default:
        UNREACHABLE();
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, LabelType t)
{
    switch (t) {
    case LabelType::Begin:
        os << L"start";
        break;
    case LabelType::Top:
        os << L"top";
        break;
    case LabelType::Else:
        os << L"else";
        break;
    case LabelType::End:
        os << L"end";
        break;
    default:
        UNREACHABLE();
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, QBELabel const &label)
{
    os << '@' << label.type << '_' << label.node->id.value();
    return os;
}

std::wostream &operator<<(std::wostream &os, AllocDef const &impl)
{
    os << "    " << impl.target << " = l alloc" << impl.alignment << " " << impl.bytes;
    return os;
}

std::wostream &operator<<(std::wostream &os, BlitDef const &impl)
{
    os << "    blit " << impl.src << ", " << impl.dest << ", " << impl.bytes;
    return os;
}

std::wostream &operator<<(std::wostream &os, CallDef const &impl)
{
    os << "    ";
    if (size_of(impl.target.type) > 0) {
        os << impl.target << " = " << impl.target.type << ' ';
    }
    std::wstring_view n { impl.name };
    if (auto colon = n.rfind(L':'); colon != std::wstring_view::npos) {
        n = std::wstring_view { n }.substr(colon + 1);
    }
    os << "call $" << n << '(';
    auto first { true };
    for (auto const &arg : impl.args) {
        if (!first) {
            os << ", ";
        }
        first = false;
        std::visit(
            overloads {
                [&os, &arg](auto const &) {
                    std::visit(
                        overloads {
                            [](std::monostate const &) {
                                UNREACHABLE();
                            },
                            [&os](ILBaseType const &bt) {
                                os << must_extend(bt);
                            },
                            [&os](ILType::ILAggregate const &t) {
                                os << t.name;
                            } },
                        arg.type.inner);
                } },
            arg.inner);
        os << " " << arg;
    }
    os << ")";
    if (n != impl.name) {
        os << " # " << impl.name;
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, CastDef const &impl)
{
    os << "    " << impl.target << " = " << impl.target.type << " cast " << impl.expr;
    return os;
}

std::wostream &operator<<(std::wostream &os, CopyDef const &impl)
{
    os << "    " << impl.target << " = " << targettype(impl.target.type) << " copy " << impl.expr;
    return os;
}

std::wostream &operator<<(std::wostream &os, DbgFile const &impl)
{
    os << "dbgfile \"" << impl.name << '"';
    return os;
}

std::wostream &operator<<(std::wostream &os, DbgLoc const &impl)
{
    os << "    dbgloc " << impl.line << ", " << impl.column;
    if (!impl.comment.empty()) {
        os << " # " << impl.comment;
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, ExprDef const &impl)
{
    std::wstringstream op;
    assert(std::holds_alternative<ILBaseType>(impl.target.type.inner));
    auto t = std::get<ILBaseType>(impl.target.type.inner);
    if (impl.op >= ILOperation::Equals) {
        op << 'c';
        if (impl.op >= ILOperation::GreaterEqual) {
            switch (t) {
            case ILBaseType::UB:
            case ILBaseType::UH:
            case ILBaseType::UW:
                op << 'u';
                break;
            case ILBaseType::S:
            case ILBaseType::D:
                break;
            default:
                op << 's';
                break;
            }
        }
    }
    op << impl.op;
    if (impl.op >= ILOperation::Equals) {
        op << basetype(impl.lhs.type);
    }
    os << "    " << impl.target << " = " << targettype(impl.target.type) << " " << op.str() << " " << impl.lhs << ", " << impl.rhs;
    return os;
}

std::wostream &operator<<(std::wostream &os, ExtDef const &impl)
{
    auto t { get<ILBaseType>(impl.source.type.inner) };
    os << "    "
       << impl.target
       << " = "
       << targettype(impl.target.type)
       << " ext"
       << ((t == ILBaseType::W) ? ILBaseType::SW : t)
       << " "
       << impl.source;
    return os;
}

std::wostream &operator<<(std::wostream &os, HltDef const &)
{
    os << "    hlt";
    return os;
}

std::wostream &operator<<(std::wostream &os, JmpDef const &impl)
{
    os << "    jmp " << impl.label;
    return os;
}

std::wostream &operator<<(std::wostream &os, JnzDef const &impl)
{
    os << "    jnz " << impl.expr << ", " << impl.on_true << ", " << impl.on_false;
    return os;
}

std::wostream &operator<<(std::wostream &os, LabelDef const &impl)
{
    os << impl.label;
    return os;
}

std::wostream &operator<<(std::wostream &os, LoadDef const &impl)
{
    os << "    " << impl.target << " = " << targettype(impl.target.type) << " load" << impl.target.type << " " << impl.pointer;
    return os;
}

std::wostream &operator<<(std::wostream &os, NegateDef const &impl)
{
    os << "    "
       << impl.target
       << " = "
       << targettype(impl.target.type)
       << " neg "
       << impl.operand;
    return os;
}

std::wostream &operator<<(std::wostream &os, PhiDef::PhiArgDef const &impl)
{
    os << "@lbl_" << impl.come_from << " " << impl.expr;
    return os;
}

std::wostream &operator<<(std::wostream &os, PhiDef const &impl)
{
    os << "    " << impl.target << " = " << impl.type << " phi ";
    bool first { true };
    for (auto const &arg : impl.args) {
        if (!first) {
            os << ", ";
        }
        first = false;
        os << arg;
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, RetDef const &impl)
{
    os << "    ret";
    if (impl.expr) {
        os << ' ' << impl.expr.value();
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, StoreDef const &impl)
{
    os << "    store" << basetype(impl.expr.type) << ' ' << impl.expr << ", " << impl.target;
    return os;
}

std::wostream &operator<<(std::wostream &os, VaArgDef const &impl)
{
    os << "    " << impl.target << " = " << impl.type << " vaarg " << impl.arglist;
    return os;
}

std::wostream &operator<<(std::wostream &os, VaStartDef const &impl)
{
    os << "    vastart " << impl.arglist;
    return os;
}
}
