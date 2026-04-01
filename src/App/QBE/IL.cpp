/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include "App/Type.h"
#include <ranges>
#include <sstream>

#include <App/Parser.h>
#include <App/QBE/QBE.h>

namespace Lia::QBE {

bool operator==(ILType const &type, ILBaseType other)
{
    return std::holds_alternative<ILBaseType>(type.inner) && std::get<ILBaseType>(type.inner) == other;
}

bool operator==(ILBaseType const &type, ILType const &other)
{
    return std::holds_alternative<ILBaseType>(other.inner) && std::get<ILBaseType>(other.inner) == type;
}

bool operator!=(ILType const &type, ILBaseType other)
{
    return !std::holds_alternative<ILBaseType>(type.inner) || std::get<ILBaseType>(type.inner) != other;
}

bool operator!=(ILBaseType const &type, ILType const &other)
{
    return !std::holds_alternative<ILBaseType>(other.inner) || std::get<ILBaseType>(other.inner) != type;
}

bool qbe_first_class_type(pType const &type)
{
    auto const &t = type->value_type();
    return std::visit(
        overloads {
            [](IntType const &) -> bool {
                return true;
            },
            [](FloatType const &) -> bool {
                return true;
            },
            [](BoolType const &) -> bool {
                return true;
            },
            [](EnumType const &e) -> bool {
                return qbe_first_class_type(e.underlying_type);
            },
            [](ZeroTerminatedArray const &) -> bool {
                return true;
            },
            [](auto const &) -> bool {
                return false;
            },
        },
        t->description);
}

ILBaseType qbe_type_code(IntType const &type)
{
    switch (type.width_bits) {
    case 8:
        return ILBaseType::B;
    case 16:
        return ILBaseType::H;
    case 32:
        return ILBaseType::W;
    case 64:
        return ILBaseType::L;
    default:
        UNREACHABLE();
    }
}

ILBaseType qbe_type_code(FloatType const &type)
{
    switch (type.width_bits) {
    case 32:
        return ILBaseType::S;
    case 64:
        return ILBaseType::D;
    default:
        UNREACHABLE();
    }
}

ILBaseType qbe_type_code(ZeroTerminatedArray const &)
{
    return ILBaseType::L;
}

ILBaseType qbe_type_code(BoolType const &)
{
    return ILBaseType::W;
}

ILBaseType qbe_type_code(StructType const &)
{
    return ILBaseType::L;
}

ILBaseType qbe_type_code(auto const &type)
{
    int                   status;
    std::type_info const &ti = typeid(type);
    auto                 *realname = abi::__cxa_demangle(ti.name(), NULL, NULL, &status);
    warning("Assuming qbe_type_code(`{}') is `l`", realname);
    return ILBaseType::L;
}

ILBaseType qbe_type_code(pType const &type)
{
    return std::visit(
        [](auto const &descr) -> ILBaseType {
            return qbe_type_code(descr);
        },
        type->description);
}

ILBaseType qbe_load_code(pType const &type)
{
    return std::visit(
        overloads {
            [](IntType const &int_type) -> ILBaseType {
                switch (int_type.width_bits) {
                case 8:
                    return (int_type.is_signed) ? ILBaseType::SB : ILBaseType::UB;
                case 16:
                    return (int_type.is_signed) ? ILBaseType::SH : ILBaseType::UH;
                case 32:
                    return (int_type.is_signed) ? ILBaseType::SW : ILBaseType::UW;
                case 64:
                    return ILBaseType::L;
                default:
                    UNREACHABLE();
                }
            },
            [](BoolType const &) -> ILBaseType {
                return ILBaseType::W;
            },
            [](FloatType const &float_type) -> ILBaseType {
                return qbe_type_code(float_type);
            },
            [](EnumType const &enum_type) -> ILBaseType {
                return qbe_load_code(enum_type.underlying_type);
            },
            [](auto const &) -> ILBaseType {
                return ILBaseType::L;
            } },
        type->description);
}

int is_integer(ILBaseType type)
{
    return type <= ILBaseType::L;
}

int is_float(ILBaseType type)
{
    return type >= ILBaseType::S;
}

ILType::ILAggregate::ILAggregate(pType const &type)
    : name(std::move(type_ref(type)))
    , size_of(type->size_of())
    , align_of(type->align_of())
    , layout(std::move(flatten_type(type)))
{
}

ILBaseType basetype(ILType const &type)
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> ILBaseType {
                UNREACHABLE();
            },
            [](ILBaseType const &base_type) -> ILBaseType {
                return static_cast<ILBaseType>(static_cast<uint8_t>(base_type) & 0xFC);
            },
            [](auto const &) -> ILBaseType {
                return ILBaseType::L;
            } },
        type.inner);
}

void flatten_type(pType const &type, ILLayout &layout);

void flatten_type(BoolType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::W);
}

void flatten_type(EnumType const &enum_type, ILLayout &layout)
{
    flatten_type(enum_type.underlying_type, layout);
}

void flatten_type(TaggedUnionType const &tagged_union, ILLayout &layout)
{
    pType largest { TypeRegistry::void_ };
    std::ranges::for_each(
        tagged_union.tags,
        [&largest](auto const &tag) {
            if (tag.payload->size_of() > largest->size_of()) {
                largest = tag.payload;
            }
        });
    flatten_type(largest, layout);
    flatten_type(tagged_union.tag_type, layout);
}

void flatten_type(FloatType const &flt_type, ILLayout &layout)
{
    layout.emplace_back(qbe_type_code(flt_type));
}

void flatten_type(IntType const &int_type, ILLayout &layout)
{
    layout.emplace_back(qbe_type_code(int_type));
}

void flatten_type(PointerType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::L);
}

void flatten_type(ReferenceType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::L);
}

void flatten_type(SliceType const &, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::L);
    layout.emplace_back(ILBaseType::L);
}

void flatten_type(StructType const &strukt, ILLayout &layout)
{
    for (auto const &field : strukt.fields) {
        flatten_type(field.type, layout);
    }
}

void flatten_type(TypeType const &type_type, ILLayout &layout)
{
    flatten_type(type_type.type, layout);
}

void flatten_type(OptionalType const &opt, ILLayout &layout)
{
    flatten_type(opt.type, layout);
    layout.emplace_back(ILBaseType::W);
}

void flatten_type(ResultType const &result, ILLayout &layout)
{
    layout.emplace_back(ILBaseType::W);
    flatten_type((result.success->size_of() > result.error->size_of()) ? result.success : result.error);
}

void flatten_type(VoidType const &, ILLayout &)
{
}

void flatten_type(ZeroTerminatedArray const &, ILLayout &)
{
}

void flatten_type(auto const &descr, std::vector<ILBaseType> &)
{
    NYI("flatten_type for {}", typeid(descr).name());
}

void flatten_type(pType const &type, std::vector<ILBaseType> &components)
{
    std::vector<ILBaseType> ret {};
    std::visit(
        [&components](auto const &descr) {
            return flatten_type(descr, components);
        },
        type->description);
}

ILLayout flatten_type(pType const &type)
{
    ILLayout ret;
    flatten_type(type, ret);
    return ret;
}

ILType qbe_type(pType const &type)
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> ILType {
                return {};
            },
            [](VoidType const &) -> ILType {
                return {};
            },
            [](BoolType const &) -> ILType {
                return ILBaseType::W;
            },
            [](FloatType const &flt_type) -> ILType {
                switch (flt_type.width_bits) {
                case 32:
                    return ILBaseType::S;
                case 64:
                    return ILBaseType::D;
                default:
                    UNREACHABLE();
                }
            },
            [](IntType const &int_type) -> ILType {
                switch (int_type.width_bits) {
                case 8:
                    return (int_type.is_signed) ? ILBaseType::SB : ILBaseType::UB;
                case 16:
                    return (int_type.is_signed) ? ILBaseType::SH : ILBaseType::UH;
                case 32:
                    return ILBaseType::W;
                case 64:
                    return ILBaseType::L;
                default:
                    UNREACHABLE();
                }
            },
            [](ReferenceType const &reference) -> ILType {
                return ILBaseType::L;
            },
            [](ZeroTerminatedArray const &) -> ILType {
                return ILBaseType::L;
            },
            [&type](EnumType const &enum_type) -> ILType {
                return qbe_type(enum_type.underlying_type);
            },
            [&type](auto const &descr) -> ILType {
                return ILType { type };
            } },
        type->description);
}

ILBaseType must_extend(ILType const &type)
{
    return std::visit(
        overloads {
            [](ILBaseType const &base_type) -> ILBaseType {
                switch (base_type) {
                case ILBaseType::B:
                case ILBaseType::SB:
                    return ILBaseType::SB;
                case ILBaseType::UB:
                    return ILBaseType::UB;
                case ILBaseType::H:
                case ILBaseType::SH:
                    return ILBaseType::SH;
                case ILBaseType::UH:
                    return ILBaseType::UH;
                case ILBaseType::W:
                case ILBaseType::SW:
                case ILBaseType::UW:
                    return ILBaseType::W;
                case ILBaseType::L:
                    return ILBaseType::L;
                default:
                    UNREACHABLE();
                }
            },
            [](auto const &) -> ILBaseType {
                UNREACHABLE();
            } },
        type.inner);
}

ILBaseType targettype(ILType const &type)
{
    return std::visit(
        overloads {
            [](std::monostate const &) -> ILBaseType {
                UNREACHABLE();
            },
            [](ILBaseType const &base_type) -> ILBaseType {
                switch (base_type) {
                case ILBaseType::L:
                case ILBaseType::S:
                case ILBaseType::D:
                case ILBaseType::W:
                    return base_type;
                default:
                    return ILBaseType::W;
                }
            },
            [](auto const &) -> ILBaseType {
                return ILBaseType::L;
            } },
        type.inner);
}

int align_of(ILBaseType const &type)
{
    switch (type) {
#undef S
#define S(T, Code, Str, Align, Size) \
    case ILBaseType::T:              \
        return Align;                \
        break;
        ILBASETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

int align_of(ILType::ILAggregate const &type)
{
    return type.align_of;
}

int align_of(std::monostate const &)
{
    return 0;
}

int size_of(ILBaseType const &type)
{
    switch (type) {
#undef S
#define S(T, Code, Str, Align, Size) \
    case ILBaseType::T:              \
        return Size;                 \
        break;
        ILBASETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
}

int size_of(ILType::ILAggregate const &type)
{
    return type.size_of;
}

int size_of(std::monostate const &)
{
    return 0;
}

int align_of(ILType const &type)
{
    return std::visit([](auto const &t) -> int {
        return align_of(t);
    },
        type.inner);
}

int size_of(ILType const &type)
{
    return std::visit([](auto const &t) -> int {
        return size_of(t);
    },
        type.inner);
}

std::wostream &operator<<(std::wostream &os, ILBaseType const &type)
{
    switch (type) {
#undef S
#define S(T, Code, Str, Align, Size) \
    case ILBaseType::T:              \
        os << #Str;                  \
        break;
        ILBASETYPES(S)
#undef S
    default:
        UNREACHABLE();
    }
    return os;
}

std::wostream &operator<<(std::wostream &os, ILType::ILAggregate const &aggregate)
{
    return os << aggregate.name;
}

std::wostream &operator<<(std::wostream &os, ILType const &type)
{
    std::visit(
        [&os](auto const &i) {
            os << i;
        },
        type.inner);
    return os;
}

std::wostream &operator<<(std::wostream &os, ILOperation const &op)
{
    switch (op) {
#undef S
#define S(Op, Str, LiaOp) \
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

std::wostream &operator<<(std::wostream &os, ILValue const &value)
{
    std::visit(
        overloads {
            [&os](ILValue::Local const &local) {
                os << "%local_" << local.var;
            },
            [&os](ILValue::Global const &global) {
                os << "$" << global.name;
            },
            [&os](std::wstring const &literal) {
                os << literal;
            },
            [&os](ILValue::Temporary const &temporary) {
                os << "%temp_" << temporary.index;
            },
            [&os](ILValue::Variable const &var) {
                os << "%var_" << var.index;
            },
            [&os](ILValue::Parameter const &param) {
                os << "%param_" << param.index;
            },
            [&os](ILValue::ReturnValue const &) {
                os << "%ret$";
            },
            [&os, &value](double const &dbl) {
                std::visit(
                    overloads {
                        [&os](ILBaseType const &type) {
                            switch (type) {
                            case ILBaseType::D:
                                os << "d_";
                                break;
                            case ILBaseType::S:
                                os << "s_";
                                break;
                            default:
                                break;
                            }
                        },
                        [](auto const &t) {
                            UNREACHABLE();
                        } },
                    value.type.inner);
                os << dbl;
            },
            [&os](int64_t const &i) {
                os << i;
            },
            [&os](std::vector<ILValue> const &seq) {
                auto first { true };
                for (auto const &v : seq) {
                    if (!first) {
                        os << ", ";
                    }
                    first = false;
                    os << v;
                }
            },
        },
        value.inner);
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

std::wostream &operator<<(std::wostream &os, HltDef const &impl)
{
    os << "    hlt";
    return os;
}

std::wostream &operator<<(std::wostream &os, JmpDef const &impl)
{
    os << "    jmp @lbl_" << impl.label;
    return os;
}

std::wostream &operator<<(std::wostream &os, JnzDef const &impl)
{
    os << "    jnz " << impl.expr << ", @lbl_" << impl.on_true << ", @lbl_" << impl.on_false;
    return os;
}

std::wostream &operator<<(std::wostream &os, LabelDef const &impl)
{
    os << "@lbl_" << impl.label;
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

std::wostream &operator<<(std::wostream &os, ILFunction const &function)
{
    if (function.exported) {
        os << "export ";
    }
    os << "function " << qbe_type(function.return_type) << " $" << function.name << '(';
    auto first = true;
    for (auto const &[ix, param] : std::ranges::views::enumerate(function.parameters)) {
        if (!first) {
            os << ", ";
        }
        first = false;
        os << param.il_type << " %param_" << ix;
    }
    os << R"() {
@start
)";
    if (function.return_type->size_of() > 8) {
        os << AllocDef {
            8,
            static_cast<size_t>(function.return_type->size_of()),
            ILValue::return_value(ILBaseType::L),
        } << '\n';
    }
    info(L"<< ILFunction {} variables {}", function.name, function.variables.size());
    for (auto const &binding : function.variables) {
        os << AllocDef {
            (binding.type->size_of() < 8) ? 4ul : 8ul,
            static_cast<size_t>(binding.type->size_of()),
            ILValue::variable(binding.index, binding.type),
        } << '\n';
    }
    for (auto const &temp : function.temps) {
        os << AllocDef {
            (temp.type->size_of() < 8) ? 4ul : 8ul,
            static_cast<size_t>(temp.type->size_of()),
            ILValue::temporary(temp.index, temp.type),
        } << '\n';
    }
    for (auto const &instruction : function.instructions) {
        os << instruction;
    }
    os << R"(}

)";
    return os;
}

std::wstring type_ref(pType const &type)
{
    if (std::holds_alternative<SliceType>(type->description)) {
        return L":slice_t";
    }
    if (auto prefix = std::visit(
            overloads {
                [](OptionalType const &) -> wchar_t const * {
                    return L"opt";
                },
                [](StructType const &) -> wchar_t const * {
                    return L"struct";
                },
                [](ResultType const &) -> wchar_t const * {
                    return L"res";
                },
                [](TaggedUnionType const &) -> wchar_t const * {
                    return L"union";
                },
                [&type](auto const &) -> wchar_t const * {
                    return nullptr;
                } },
            type->description);
        prefix != nullptr) {
        return std::format(L":{}{}", prefix, *(type.id));
    } else {
        auto                t { qbe_type_code(type) };
        std::wostringstream ss;
        ss << t;
        return ss.str();
    }
}

void emit_type(pType const &type, std::wostream &os)
{
    std::visit(
        overloads {
            [&os, &type](SliceType const &opt) {
                os << "type :slice_t = { l, l }\n";
            },
            [&os, &type](OptionalType const &opt) {
                os << "type "
                   << type_ref(type) << " = { "
                   << type_ref(opt.type) << ", b }\n";
            },
            [&os, &type](StructType const &strukt) {
                os << L"type " << type_ref(type) << " = { ";
                std::ranges::for_each(
                    strukt.fields | std::ranges::views::enumerate,
                    [&os](auto const &f) {
                        auto const &[ix, fld] = f;
                        if (ix > 0) {
                            os << ", ";
                        }
                        os << type_ref(fld.type);
                    });
                os << " }\n";
            },
            [&os, &type](ResultType const &result) {
                os << "type "
                   << type_ref(type) << "$_union = { { "
                   << type_ref(result.success) << " } { "
                   << type_ref(result.error) << " } }\n";
                os << "type "
                   << type_ref(type) << " = { "
                   << type_ref(type) << "$_union, b }\n";
            },
            [&os, &type](TaggedUnionType const &tagged_union) {
                os << "type "
                   << type_ref(type) << "$_union = { ";
                std::ranges::for_each(
                    tagged_union.tags,
                    [&os](TaggedUnionType::Tag const &tag) {
                        if (tag.payload != nullptr && tag.payload != TypeRegistry::void_) {
                            os << "{ "
                               << type_ref(tag.payload)
                               << " } ";
                        }
                    });
                os << " }\n"
                   << "type "
                   << type_ref(type) << " = { "
                   << type_ref(type) << "$_union, "
                   << type_ref(tagged_union.underlying())
                   << " }\n";
            },
            [&os, &type](auto const &) {
            } },
        type->description);
}

std::wostream &operator<<(std::wostream &os, ILFile const &file)
{
    for (auto const type : file.types) {
        emit_type(type, os);
    }
    if (!file.types.empty()) {
        os << '\n';
    }
    os << DbgFile { file.name } << '\n';
    for (auto const &function : file.functions) {
        os << function;
    }
    for (auto const &[ix, s] : std::ranges::views::enumerate(file.strings)) {
        os << "data $str_" << ix << " = { ";
        for (auto ch : s) {
            os << std::format(L"w {:d}, ", ch);
        }
        os << "w 0 }\n";
    }
    for (auto const &[ix, s] : std::ranges::views::enumerate(file.cstrings)) {
        os << "data $cstr_" << ix << " = { ";
        for (auto ch : s) {
            os << std::format(L"b {:d}, ", ch);
        }
        os << L"b 0 }\n";
    }
    if (!file.enumerations.empty()) {
        os << '\n';
        std::ranges::for_each(
            file.enumerations | std::views::enumerate,
            [&os](auto const &tuple) {
                auto const &[enum_id, enum_type] = tuple;
                assert(std::holds_alternative<EnumType>(enum_type->description));
                auto const &e { std::get<EnumType>(enum_type->description) };
                os << "data $enum$_" << enum_id + 1 << " = { l " << e.values.size() << ", ";
                std::ranges::for_each(
                    e.values,
                    [&os](EnumType::Value const &v) {
                        os << "l " << v.value << ", l " << v.label.length() << ", ";
                    });
                std::ranges::for_each(
                    e.values | std::views::enumerate,
                    [&os](auto const &tuple) {
                        auto &[ix, v] = tuple;
                        if (ix > 0) {
                            os << ", ";
                        }
                        for (auto ch : v.label) {
                            os << std::format(L"w {:d}, ", ch);
                        }
                        os << "w 0";
                    });
                os << " }\n";
            });
    }
    return os;
}

ILFunction::ILFunction(ILFile &file, std::wstring name, pType return_type, bool exported)
    : file_id(file.id)
    , name(std::move(name))
    , return_type(return_type)
    , exported(exported)
    , id(file.functions.size())
{
}

ILBinding const &ILFunction::add(std::wstring_view name, pType const &type)
{
    info(L"Adding {} size {}", name, variables.size());
    auto &ret { variables.emplace_back(std::wstring { name }, type, variables.size(), qbe_type(type)) };
    info(L"After adding {} size {}", name, variables.size());
    return ret;
}

ILParameter const &ILFunction::add_parameter(std::wstring_view name, pType const &type)
{
    std::optional<int> var_index {};
    if (!is<ReferenceType>(type)) {
        var_index = variables.size();
    }
    return parameters.emplace_back(std::wstring { name }, type, parameters.size(), var_index, qbe_type(type));
}

ILTemporary const &ILFunction::add_temporary(pType const &type, ILType il_type)
{
    return temps.emplace_back(temps.size(), type, std::move(il_type));
}

}
