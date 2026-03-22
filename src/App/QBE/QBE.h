/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include <Util/Logging.h>

#include <App/Operator.h>
#include <App/SyntaxNode.h>

namespace Lia::QBE {

using namespace Util;
namespace fs = std::filesystem;

#define QBE_ASSERT(expr, ctx)                                            \
    do {                                                                 \
        if (!(expr)) {                                                   \
            fatal("{}:{} Assertion failed: " #expr, __FILE__, __LINE__); \
        }                                                                \
    } while (0)

#define ILBASETYPES(S)     \
    S(V, 0x00, void, 0, 0) \
    S(B, 0x04, b, 1, 1)    \
    S(SB, 0x05, sb, 1, 1)  \
    S(UB, 0x06, ub, 1, 1)  \
    S(H, 0x08, h, 2, 2)    \
    S(SH, 0x09, sh, 2, 2)  \
    S(UH, 0x0A, uh, 2, 2)  \
    S(W, 0x10, w, 4, 4)    \
    S(SW, 0x11, sw, 4, 4)  \
    S(UW, 0x12, uw, 4, 4)  \
    S(L, 0x20, l, 8, 8)    \
    S(S, 0x40, s, 4, 4)    \
    S(D, 0x80, d, 8, 8)

enum class ILBaseType {
#undef S
#define S(T, Code, Str, Align, Size) T = Code,
    ILBASETYPES(S)
#undef S
};

using ILLayout = std::vector<ILBaseType>;

struct ILType {

    struct ILAggregate {
        std::wstring name;
        intptr_t     size_of;
        intptr_t     align_of;
        ILLayout     layout;

        ILAggregate(pType const &type);
    };

    std::variant<ILBaseType, ILAggregate> inner;

    ILType() = default;
    ILType(ILType const &) = default;

    ILType(ILBaseType const &bt)
        : inner(bt)
    {
    }

    ILType(pType const &type)
        : inner(ILAggregate { type })
    {
    }
};

bool operator==(ILType const &type, ILBaseType other);
bool operator!=(ILType const &type, ILBaseType other);
bool operator==(ILBaseType const &type, ILType const &other);
bool operator!=(ILBaseType const &type, ILType const &other);

std::wostream &operator<<(std::wostream &os, ILBaseType const &type);
std::wostream &operator<<(std::wostream &os, ILType::ILAggregate const &aggregate);
std::wostream &operator<<(std::wostream &os, ILType const &type);

ILBaseType   basetype(ILType const &type);
ILBaseType   must_extend(ILType const &type);
ILBaseType   targettype(ILType const &type);
int          is_integer(ILBaseType type);
int          is_float(ILBaseType type);
int          align_of(ILBaseType const &type);
int          size_of(ILBaseType const &type);
int          align_of(ILType const &type);
int          size_of(ILType const &type);
std::wstring type_ref(pType const &type);
bool         qbe_first_class_type(pType const &type);
ILBaseType   qbe_type_code(pType const &type);
ILBaseType   qbe_load_code(pType const &type);
ILType       qbe_type(pType const &type);
ILLayout     flatten_type(pType const &type);

template<typename T>
ILBaseType il_type_for()
{
    fatal("Specialize il_type_for<{}>()", typeid(T).name());
}

template<>
inline ILBaseType il_type_for<bool>()
{
    return ILBaseType::W;
}

template<>
inline ILBaseType il_type_for<int8_t>()
{
    return ILBaseType::B;
}

template<>
inline ILBaseType il_type_for<uint8_t>()
{
    return ILBaseType::UB;
}

template<>
inline ILBaseType il_type_for<int16_t>()
{
    return ILBaseType::H;
}

template<>
inline ILBaseType il_type_for<uint16_t>()
{
    return ILBaseType::UH;
}

template<>
inline ILBaseType il_type_for<int32_t>()
{
    return ILBaseType::W;
}

template<>
inline ILBaseType il_type_for<uint32_t>()
{
    return ILBaseType::UW;
}

template<>
inline ILBaseType il_type_for<int64_t>()
{
    return ILBaseType::L;
}

template<>
inline ILBaseType il_type_for<uint64_t>()
{
    return ILBaseType::L;
}

template<>
inline ILBaseType il_type_for<void *>()
{
    return ILBaseType::L;
}

template<>
inline ILBaseType il_type_for<float>()
{
    return ILBaseType::S;
}

template<>
inline ILBaseType il_type_for<double>()
{
    return ILBaseType::D;
}

enum class ILInstructionType {
    Alloc,
    Blit,
    Call,
    Cast,
    Copy,
    Expr,
    Hlt,
    Jmp,
    Jnz,
    Label,
    Load,
    Phi,
    Ret,
    Store,
    VaArg,
    VaStart,
};

#define ILOPERATIONS(S)                         \
    S(Add, add, Operator::Add)                  \
    S(And, and, Operator::BinaryAnd)            \
    S(Div, div, Operator::Divide)               \
    S(Mul, mul, Operator::Multiply)             \
    S(Neg, neg, Operator::Negate)               \
    S(Or, or, Operator::BinaryOr)               \
    S(Mod, rem, Operator::Modulo)               \
    S(Sar, sar, Operator::ShiftRight)           \
    S(Shl, shl, Operator::ShiftLeft)            \
    S(Shr, shr, Operator::ShiftRight)           \
    S(Sub, sub, Operator::Subtract)             \
    S(UDiv, udiv, Operator::Divide)             \
    S(UMod, urem, Operator::Modulo)             \
    S(Xor, xor, Operator::BinaryXor)            \
    S(Equals, eq, Operator::Equals)             \
    S(NotEqual, ne, Operator::NotEqual)         \
    S(GreaterEqual, ge, Operator::GreaterEqual) \
    S(Greater, gt, Operator::Greater)           \
    S(LessEqual, le, Operator::LessEqual)       \
    S(Less, lt, Operator::Less)

enum class ILOperation {
#undef S
#define S(Op, Str, LiaOp) Op,
    ILOPERATIONS(S)
#undef S
};

struct ILValue {
    struct Local {
        int var;
    };

    struct Variable {
        int depth;
        int index;

        std::wstring name() const
        {
            return std::format(L"var_{}.{}", depth, index);
        }
    };

    struct Parameter {
        int index;

        std::wstring name() const
        {
            return std::format(L"param_{}", index);
        }
    };

    struct ReturnValue {
    };

    struct Global {
        std::wstring name;
    };

    struct Literal {
        std::wstring literal;
    };

    using ILValueInner = std::variant<
        Local,
        Global,
        Literal,
        Variable,
        Parameter,
        ReturnValue,
        int64_t,
        double,
        std::vector<ILValue>>;

    ILValue()
        : type()
        , inner(0)
    {
    }

    ILValue(ILValue const &other)
        : type(other.type)
        , inner(other.inner)
    {
    }

    template<typename T>
    ILValue(ILType const &type, T const &value)
        : type(type)
        , inner(value)
    {
    }

    operator bool() const
    {
        return size_of(type) == 0;
    }

    template<typename TypeDesc>
    static ILValue local(int var, TypeDesc td)
    {
        ILType t { std::move(td) };
        return { t, Local { var } };
    }

    static ILValue pointer(int ptr)
    {
        return { ILType { ILBaseType::L }, Local { ptr } };
    }

    template<typename TypeDesc>
    static ILValue global(std::wstring name, TypeDesc td)
    {
        return { ILType { std::move(td) }, Global { std::move(name) } };
    }

    template<typename TypeDesc>
    static ILValue literal(std::wstring literal, TypeDesc td)
    {
        return { ILType { std::move(td) }, Literal { std::move(literal) } };
    }

    template<typename TypeDesc>
    static ILValue variable(int depth, int index, TypeDesc td)
    {
        return { ILType { std::move(td) }, Variable { depth, index } };
    }

    template<typename TypeDesc>
    static ILValue parameter(int index, TypeDesc td)
    {
        return { ILType { std::move(td) }, Parameter { index } };
    }

    static ILValue string(int str_id)
    {
        return global(std::format(L"str_{}", str_id), ILBaseType::L);
    }

    static ILValue cstring(int str_id)
    {
        return global(std::format(L"cstr_{}", str_id), ILBaseType::L);
    }

    template<typename TypeDesc>
    static ILValue float_val(double d, TypeDesc td)
    {
        ILType t { std::move(td) };
        return { t, d };
    }

    template<typename TypeDesc>
    static ILValue integer(int64_t i, TypeDesc td)
    {
        ILType  t { std::move(td) };
        ILValue ret { t, i };
        return ret;
    }

    using ILValues = std::vector<ILValue>;

    static ILValue sequence(ILValues values, int align, int size)
    {
        return { ILType { ILBaseType::V }, std::move(values) };
    }

    template<typename TypeDesc>
    static ILValue return_value(TypeDesc td)
    {
        return { ILType { std::move(td) }, ReturnValue {} };
    }

    static ILValue null()
    {
        return { ILType {}, 0 };
    }

    ILType       type { ILBaseType::V };
    ILValueInner inner;
};

using ILValues = ILValue::ILValues;

std::wostream &operator<<(std::wostream &os, ILValue const &value);
std::wostream &operator<<(std::wostream &os, ILOperation const &op);

struct AllocDef {
    size_t  alignment;
    size_t  bytes;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, AllocDef const &impl);
};

struct BlitDef {
    ILValue  src;
    ILValue  dest;
    intptr_t bytes;

    friend std::wostream &operator<<(std::wostream &os, BlitDef const &impl);
};

struct CallDef {
    std::wstring         name;
    ILValue              target;
    std::vector<ILValue> args;

    friend std::wostream &operator<<(std::wostream &os, CallDef const &impl);
};

struct CastDef {
    ILValue expr;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, CastDef const &impl);
};

struct CopyDef {
    ILValue expr;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, CopyDef const &impl);
};

struct ExprDef {
    ILValue     lhs;
    ILValue     rhs;
    ILOperation op;
    ILValue     target;

    friend std::wostream &operator<<(std::wostream &os, ExprDef const &impl);
};

struct ExtDef {
    ILValue source;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, ExtDef const &impl);
};

struct HltDef {
    friend std::wostream &operator<<(std::wostream &os, HltDef const &impl);
};

struct JmpDef {
    int label;

    friend std::wostream &operator<<(std::wostream &os, JmpDef const &impl);
};

struct JnzDef {
    ILValue expr;
    int     on_true;
    int     on_false;

    friend std::wostream &operator<<(std::wostream &os, JnzDef const &impl);
};

struct LabelDef {
    int label;

    friend std::wostream &operator<<(std::wostream &os, LabelDef const &impl);
};

struct LoadDef {
    ILValue pointer;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, LoadDef const &impl);
};

struct NegateDef {
    ILValue operand;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, NegateDef const &impl);
};

struct PhiDef {
    struct PhiArgDef {
        int     come_from;
        ILValue expr;

        friend std::wostream &operator<<(std::wostream &os, PhiArgDef const &impl);
    };

    std::vector<PhiArgDef> args;
    ILBaseType             type;
    ILValue                target;

    friend std::wostream &operator<<(std::wostream &os, PhiDef const &impl);
};

struct RetDef {
    std::optional<ILValue> expr;

    friend std::wostream &operator<<(std::wostream &os, RetDef const &impl);
};

struct StoreDef {
    ILValue expr;
    ILValue target;

    friend std::wostream &operator<<(std::wostream &os, StoreDef const &impl);
};

struct VaArgDef {
    std::wstring arglist;
    ILBaseType   type;
    ILValue      target;

    friend std::wostream &operator<<(std::wostream &os, VaArgDef const &impl);
};

struct VaStartDef {
    std::wstring arglist;

    friend std::wostream &operator<<(std::wostream &os, VaStartDef const &impl);
};

using ILInstructionImpl = std::variant<
    AllocDef,
    BlitDef,
    CallDef,
    CastDef,
    CopyDef,
    ExprDef,
    ExtDef,
    HltDef,
    JmpDef,
    JnzDef,
    LabelDef,
    LoadDef,
    NegateDef,
    PhiDef,
    RetDef,
    StoreDef,
    VaArgDef,
    VaStartDef>;

struct ILInstruction {
    ILInstructionImpl impl;

    friend std::wostream &operator<<(std::wostream &os, ILInstruction const &instr)
    {
        std::visit([&os](auto const &impl) { os << impl << '\n'; }, instr.impl);
        return os;
    }
};

struct ILFunction;
struct ILFile;
struct ILProgram;

template<class N>
concept ir_node = std::is_same_v<N, ILFile>
    || std::is_same_v<N, ILFunction>;

struct ILBinding {
    std::wstring name;
    pType        type;
    int          depth;
    int          index;
    ILType       il_type;
};

using ILBindings = std::vector<ILBinding>;
using ILBindingStack = std::vector<ILBindings>;

struct ILFunction {
    size_t                     file_id;
    std::wstring               name;
    pType                      return_type;
    bool                       exported { false };
    intptr_t                   ret_allocation { 0 };
    size_t                     id;
    ILBindings                 parameters {};
    ILBindingStack             variables {};
    std::vector<ILInstruction> instructions;
    std::vector<size_t>        labels;

    std::optional<ILBinding> find(std::wstring_view name);
    ILBinding const         &add(std::wstring_view name, pType const &type);
    ILBinding const         &add_parameter(std::wstring_view name, pType const &type);
    void                     push();
    void                     pop();
    friend std::wostream    &operator<<(std::wostream &os, ILFunction const &function);
};

struct ILFile {
    std::wstring              name;
    size_t                    id;
    std::vector<pType>        types;
    std::vector<pType>        enumerations;
    std::vector<std::wstring> strings;
    std::vector<std::string>  cstrings;
    std::vector<ILFunction>   functions;
    bool                      has_exports { false };
    bool                      has_main { false };
    friend std::wostream     &operator<<(std::wostream &os, ILFunction const &function);
};

struct ILProgram {
    std::wstring        name;
    std::vector<ILFile> files;
};

struct QBEValue {
    ILBaseType                     type { ILBaseType::L };
    std::variant<intptr_t, double> payload;

    QBEValue() = default;
    QBEValue(QBEValue const &) = default;

    QBEValue(ILBaseType type, intptr_t value)
        : type(type)
        , payload(value)
    {
        assert(is_integer(type));
    }

    QBEValue(ILBaseType type, double value)
        : type(type)
        , payload(value)
    {
        assert(is_float(type));
    }

    template<typename T>
    QBEValue(T)
    {
        std::unreachable();
    }

    template<std::integral T>
    QBEValue(T val)
        : type(il_type_for<T>())
        , payload(static_cast<intptr_t>(val))
    {
    }

    template<std::floating_point T>
    QBEValue(T val)
        : type(il_type_for<T>())
        , payload(static_cast<double>(val))
    {
    }

    template<typename T>
    QBEValue(T const *val)
        : type(ILBaseType::L)
        , payload(reinterpret_cast<intptr_t>(val))
    {
    }

    template<typename T>
    QBEValue(T *val)
        : type(ILBaseType::L)
        , payload(reinterpret_cast<intptr_t>(val))
    {
    }

    operator intptr_t() const
    {
        assert(std::holds_alternative<intptr_t>(payload));
        return std::get<intptr_t>(payload);
    }

    operator double() const
    {
        assert(std::holds_alternative<double>(payload));
        return std::get<double>(payload);
    }

    template<std::integral T>
    operator T() const
    {
        return static_cast<T>(operator intptr_t());
    }

    operator float() const
    {
        return static_cast<float>(operator double());
    }

    operator bool() const
    {
        return static_cast<bool>(operator intptr_t());
    }

    template<typename T>
    operator T *() const
    {
        return reinterpret_cast<T *>(operator intptr_t());
    }

    template<typename T>
    operator T const *() const
    {
        return reinterpret_cast<T const *>(operator intptr_t());
    }

    operator std::wstring() const
    {
        Slice slice { *(reinterpret_cast<Slice *>(operator intptr_t())) };
        return std::wstring {
            reinterpret_cast<wchar_t *>(slice.ptr),
            static_cast<size_t>(slice.size)
        };
    }
};

QBEValue evaluate(QBEValue const &lhs, Operator op, QBEValue const &rhs);
QBEValue evaluate(Operator op, QBEValue const &operand);

using ILVariables = std::map<std::wstring, QBEValue>;

struct QBEContext {
    int       next_label;
    int       next_var;
    fs::path  file_name;
    bool      is_export { false };
    ILProgram program {};
    size_t    current_file;
    size_t    current_function;

    ILValue                  add_string(std::wstring_view s);
    ILValue                  add_cstring(std::string_view s);
    ILValue                  add_enumeration(pType const &enum_type);
    ILType                   qbe_type(pType const &type);
    void                     add_operation(ILInstructionImpl impl);
    std::optional<ILBinding> find(std::wstring_view name);
    ILBinding const         &add(std::wstring_view name, pType const &type);
    ILBinding const         &add_parameter(std::wstring_view name, pType const &type);
    void                     push();
    void                     pop();
    ILFunction              &function();
};

struct Frame {
    struct VM            &vm;
    ILFile const         &file;
    ILFunction const     &function;
    ILVariables           variables {};
    ILVariables           arguments {};
    QBEValue              return_value;
    std::vector<QBEValue> locals {};
    size_t                ip { 0 };

    QBEValue allocate(size_t bytes, size_t alignment);
    QBEValue allocate(ILType const &type);
    void     release(QBEValue ptr);
    QBEValue make_from_buffer(ILValue target, QBEValue val);
    void     dump_frame() const;
};

using pFrame = Ptr<Frame, struct VM>;

struct VM {
    constexpr static size_t         STACK_SIZE = 64 * 1024;
    ILProgram const                &program;
    std::vector<Frame>              frames;
    std::vector<ILVariables>        globals;
    std::array<uint8_t, STACK_SIZE> stack;
    std::array<uint8_t, STACK_SIZE> data;
    size_t                          stack_pointer { 0 };
    size_t                          data_pointer { 0 };

    VM(ILProgram const &program);
    size_t       size() const;
    bool         empty() const;
    pFrame       new_frame(ILFile const &file, ILFunction const &function);
    void         release_frame();
    Frame const &operator[](size_t ix) const;
    Frame       &operator[](size_t ix);
    QBEValue     allocate(size_t bytes, size_t alignment);
    QBEValue     allocate(ILType const &type);
    void         release(QBEValue ptr);
    void         dump_stack() const;
};

using ExecutionResult = std::expected<QBEValue, std::wstring>;

struct QBEOperand {
    ASTNode node;
    pType   ptype;

    using GenResult = std::expected<QBEOperand, std::wstring>;

    QBEOperand() = default;
    QBEOperand(QBEOperand const &) = default;
    QBEOperand(ASTNode const &n, pType const &t)
        : node(n)
        , ptype(t)
    {
    }

    QBEOperand(ASTNode const &n, pType const &t, ILValue const &value)
        : node(n)
        , ptype(t)
        , value(value)
    {
    }

    QBEOperand(ASTNode const &n)
        : node(n)
        , ptype(n->bound_type)
    {
    }

    QBEOperand(ASTNode const &n, ILValue const &value)
        : node(n)
        , ptype(n->bound_type)
        , value(value)
    {
    }

    QBEOperand(QBEOperand const &op, ILValue value)
        : node(op.node)
        , ptype(op.ptype)
        , value(value)
    {
    }

    QBEOperand(QBEOperand const &op, pType const &type, ILValue value)
        : node(op.node)
        , ptype(type)
        , value(value)
    {
    }

    GenResult      get_value(QBEContext &ctx);
    GenResult      dereference(QBEContext &ctx) const;
    GenResult      materialize(QBEContext &ctx) const;
    ILValue const &get_value() const
    {
        assert(value.has_value());
        return *value;
    }

    void set_value(ILValue const &v)
    {
        value.emplace(v);
    }

private:
    std::optional<ILValue> value {};
};

struct QBEBinExpr {
    ASTNode    node;
    QBEOperand lhs;
    Operator   op;
    QBEOperand rhs;
};

struct QBEUnaryExpr {
    ASTNode    node;
    Operator   op;
    QBEOperand operand;
};

#define TRY_DEREFERENCE(op, ctx)                            \
    (                                                       \
        {                                                   \
            QBEOperand __op;                                \
            if (auto __res = op.dereference(ctx); !__res) { \
                return std::unexpected(__res.error());      \
            } else {                                        \
                __op = (__res.value());                     \
            }                                               \
            (__op);                                         \
        })

#define TRY_MATERIALIZE(op, ctx)                            \
    (                                                       \
        {                                                   \
            QBEOperand __op;                                \
            if (auto __res = op.materialize(ctx); !__res) { \
                return std::unexpected(__res.error());      \
            } else {                                        \
                __op = (__res.value());                     \
            }                                               \
            (__op);                                         \
        })

using GenResult = QBEOperand::GenResult;

GenResult                              qbe_operator(QBEBinExpr const &expr, QBEContext &ctx);
GenResult                              qbe_operator(QBEUnaryExpr const &expr, QBEContext &ctx);
std::expected<ILProgram, std::wstring> generate_qbe(ASTNode const &node);
std::expected<void, std::wstring>      compile_qbe(ILProgram const &program);
ExecutionResult                        execute_qbe(VM &vm, ILFile const &file, ILFunction const &function, std::vector<QBEValue> const &args);
ExecutionResult                        execute_qbe(VM &vm);
bool                                   native_call(std::string_view name, uint8_t *params, std::vector<ILType> const &types, uint8_t *return_value, ILType const &return_type);
std::wostream                         &operator<<(std::wostream &os, ILFunction const &function);
std::wostream                         &operator<<(std::wostream &os, ILFile const &file);
std::wostream                         &operator<<(std::wostream &os, QBEValue const &value);

template<typename T>
T as(VM const &vm, QBEValue const &val)
{
    std::unreachable();
}

}

template<>
struct std::formatter<Lia::QBE::ILBaseType, wchar_t> {

    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it == ctx.end() || *it == '}') {
            return it;
        }
        ++it;
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(Lia::QBE::ILBaseType const &type, FmtContext &ctx) const
    {
        std::wostringstream out;
        out << type;
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<Lia::QBE::ILType, wchar_t> {

    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it == ctx.end() || *it == '}') {
            return it;
        }
        ++it;
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(Lia::QBE::ILType const &type, FmtContext &ctx) const
    {
        std::wostringstream out;
        std::visit(
            overloads {
                [&out](std::monostate const &) {
                    out << L"(void)";
                },
                [&out](auto const &inner) {
                    out << inner;
                } },
            type.inner);
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<Lia::QBE::ILValue, wchar_t> {
    using ILValue = Lia::QBE::ILValue;

    bool with_type { false };

    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it == ctx.end() || *it == '}')
            return it;

        switch (*it) {
        case 't':
            with_type = true;
            break;
        default:
            throw std::format_error("Invalid format args for Value");
        }
        ++it;
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("Invalid format args for Value");
        }
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(ILValue const &value, FmtContext &ctx) const
    {
        std::wostringstream out;
        if (with_type) {
            std::visit(
                [&out](auto const &inner) {
                    out << demangle<decltype(inner)>() << ' ';
                },
                value.inner);
        }
        out << value;
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<Lia::QBE::ILInstruction, wchar_t> {
    using ILInstruction = Lia::QBE::ILInstruction;

    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        if (it == ctx.end() || *it == '}') {
            return it;
        }
        ++it;
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(ILInstruction const &value, FmtContext &ctx) const
    {
        std::wostringstream out;
        out << value;
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

template<>
struct std::formatter<Lia::QBE::QBEValue, wchar_t> {
    using QBEValue = Lia::QBE::QBEValue;

    bool with_type { false };
    int  radix { 10 };

    template<class ParseContext>
    constexpr ParseContext::iterator parse(ParseContext &ctx)
    {
        auto it = ctx.begin();
        while (it != ctx.end() && *it != '}') {
            switch (*it) {
            case 't':
                with_type = true;
                break;
            case 'x':
                radix = 16;
                break;
            default:
                throw std::format_error("Invalid format args for Value");
            }
            ++it;
        }
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(QBEValue const &value, FmtContext &ctx) const
    {
        std::wostringstream out;
        if (with_type) {
            std::visit(
                [&out](auto const &payload) {
                    out << demangle<decltype(payload)> << ' ';
                },
                value.payload);
        }
        switch (radix) {
        case 16:
            out << "0x" << std::hex << value;
            break;
        default:
            out << value;
            break;
        }
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};
