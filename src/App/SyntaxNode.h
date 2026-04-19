/*
 * Copyright (c) 2025, Jan de Visser <jan@finiandarcy.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstddef>
#include <expected>
#include <map>
#include <ostream>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <Util/Ptr.h>
#include <Util/TokenLocation.h>

#include <App/Operator.h>
#include <App/Type.h>

namespace Lia {

using namespace std::literals;
using namespace Util;

#define SyntaxNodeTypes(S) \
    S(Dummy)               \
    S(Alias)               \
    S(ArgumentList)        \
    S(BinaryExpression)    \
    S(Block)               \
    S(BoolConstant)        \
    S(Break)               \
    S(Call)                \
    S(Comptime)            \
    S(Continue)            \
    S(CString)             \
    S(Decimal)             \
    S(DefaultSwitchValue)  \
    S(DeferStatement)      \
    S(Embed)               \
    S(Enum)                \
    S(EnumValue)           \
    S(ExportDeclaration)   \
    S(ExpressionList)      \
    S(Extern)              \
    S(ExternLink)          \
    S(ForStatement)        \
    S(FunctionDeclaration) \
    S(FunctionDefinition)  \
    S(Identifier)          \
    S(IdentifierList)      \
    S(IfStatement)         \
    S(Include)             \
    S(Import)              \
    S(LoopStatement)       \
    S(Module)              \
    S(ModuleProxy)         \
    S(Nullptr)             \
    S(Number)              \
    S(Parameter)           \
    S(Program)             \
    S(PublicDeclaration)   \
    S(QuotedString)        \
    S(Return)              \
    S(StampedIdentifier)   \
    S(String)              \
    S(Struct)              \
    S(StructMember)        \
    S(SwitchCase)          \
    S(SwitchStatement)     \
    S(TagValue)            \
    S(TypeSpecification)   \
    S(UnaryExpression)     \
    S(VariableDeclaration) \
    S(Void)                \
    S(WhileStatement)      \
    S(Yield)

enum class SyntaxNodeType {
#undef S
#define S(T) T,
    SyntaxNodeTypes(S)
#undef S
};

enum class ASTStatus {
    Initialized,
    Normalized,
    Undetermined,
    Bound,
    Ambiguous,
    BindErrors,
    InternalError,
};

using BindError = std::unexpected<ASTStatus>;
using BindResult = std::expected<pType, ASTStatus>;
using BindResults = std::expected<pTypes, ASTStatus>;

extern char const *SyntaxNodeType_name(SyntaxNodeType type);
extern void        print_indent(std::wostream &os, int indent);

using Label = std::optional<std::wstring>;
using Strings = std::vector<std::wstring>;

struct Parser;

struct ASTNode : public Ptr<struct ASTNodeImpl, Parser> {
    ASTNode(Parser *parser, size_t id)
        : Ptr(parser, id)
    {
    }

    ASTNode(Parser *parser)
        : Ptr(parser)
    {
    }

    ASTNode() = default;
    ASTNode(ASTNode const &) = default;

    ASTNode(std::nullptr_t const &n)
        : Ptr(n)
    {
    }

    size_t        value() const;
    TokenLocation operator+(ASTNode const &other);
    TokenLocation operator+(TokenLocation const &other);
    void          error(char const *message) const { error(std::string(message)); }
    void          error(wchar_t const *message) const { error(std::wstring(message)); }
    void          error(std::wstring const &message) const;
    void          error(std::string const &message) const;

    template<typename... Args>
    void error(std::format_string<Args...> const message, Args &&...args) const
    {
        error(std::vformat(message.get(), std::make_format_args(args...)));
    }

    template<typename... Args>
    void error(std::wformat_string<Args...> const message, Args &&...args) const
    {
        error(std::vformat(message.get(), std::make_wformat_args(args...)));
    }

    BindError bind_error(std::wstring const &message) const;
    BindError bind_error(LiaError error) const;
    BindError bind_error(wchar_t const *message) const { return bind_error(std::wstring(message)); }

    template<typename... Args>
    BindError bind_error(std::wformat_string<Args...> const message, Args &&...args) const
    {
        return bind_error(std::vformat(message.get(), std::make_wformat_args(args...)));
    }
};

using ASTNodes = std::vector<ASTNode>;
using NSNode = Ptr<struct Namespace>;

struct Namespace {
    using VariableMap = std::map<std::wstring, ASTNode>;
    using TypeMap = std::map<std::wstring, pType>;
    using FunctionMap = std::map<std::wstring, ASTNodes>;
    using FunctionIter = FunctionMap::iterator;
    using FunctionConstIter = FunctionMap::const_iterator;

    constexpr static size_t Variable = 0;
    constexpr static size_t Function = 1;
    constexpr static size_t Module = 2;
    constexpr static size_t Type = 3;

    using NSEntry = std::variant<ASTNode, ASTNode, ASTNode, pType>;
    using NSEntryMap = std::multimap<std::wstring, NSEntry>;
    using NSEntries = std::vector<NSEntry>;

    NSNode     id;
    ASTNode    node;
    NSEntryMap entries;
    NSNode     parent { nullptr };

    explicit Namespace(ASTNode node, NSNode parent = nullptr);
    bool                   contains(std::wstring const &name) const;
    int                    count(std::wstring const &name) const;
    std::optional<NSEntry> at(std::wstring const &name) const;
    NSEntries              all(std::wstring const &name) const;
    pType                  type_of(std::wstring const &) const;
    ASTNode                find_module(std::wstring const &name) const;
    bool                   has_module(std::wstring const &name) const;
    void                   register_module(std::wstring const &name, ASTNode mod);
    pType                  find_type(std::wstring const &name) const;
    bool                   has_type(std::wstring const &name) const;
    void                   register_type(std::wstring name, pType type);
    void                   register_function(std::wstring name, ASTNode fnc);
    ASTNode                current_function() const;
    bool                   has_function(std::wstring const &name) const;
    ASTNodes               find_functions(std::wstring const &name) const;
    ASTNode                find_function(std::wstring const &name, pType const &type) const;
    ASTNodes               find_overloads(std::wstring const &name, ASTNodes const &type_args) const;
    bool                   has_variable(std::wstring const &name) const;
    ASTNode                find_variable(std::wstring const &name) const;
    void                   register_variable(std::wstring name, ASTNode node);
};

enum class Visibility {
    Static,
    Public,
    Export,
};

struct AbstractSyntaxNode {
    AbstractSyntaxNode() = default;
    BindResult bind(ASTNode const &n) const
    {
        return nullptr;
    }
};

struct Alias : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      aliased_type;

    Alias(std::wstring name, ASTNode aliased_type);
    BindResult bind(ASTNode const &n) const;
};

struct ArgumentList : public AbstractSyntaxNode {
    ASTNodes arguments;

    explicit ArgumentList(ASTNodes arguments);
    BindResult bind(ASTNode const &n) const;
};

struct BinaryExpression : public AbstractSyntaxNode {
    ASTNode  lhs;
    Operator op;
    ASTNode  rhs;

    BinaryExpression(ASTNode lhs, Operator op, ASTNode rhs);
    BindResult bind(ASTNode const &n) const;
};

struct Block : public AbstractSyntaxNode {
    ASTNodes statements;
    Label    label;

    Block() = default;
    Block(ASTNodes statements, Label label = { });
    BindResult bind(ASTNode const &n) const;
};

struct BoolConstant : public AbstractSyntaxNode {
    bool value;

    explicit BoolConstant(bool value);
    BindResult bind(ASTNode const &n) const;
};

struct Break : public AbstractSyntaxNode {
    Label   label;
    ASTNode block;

    explicit Break(Label label, ASTNode block = nullptr);
    BindResult bind(ASTNode const &n) const;
};

struct Call : public AbstractSyntaxNode {
    ASTNode callable;
    ASTNode arguments;
    ASTNode function;

    Call(ASTNode callable, ASTNode arguments, ASTNode function = nullptr);
    BindResult bind(ASTNode const &n) const;
};

struct Comptime : public AbstractSyntaxNode {
    std::wstring script_text;
    ASTNode      statements;
    std::wstring output;

    explicit Comptime(std::wstring_view script_text, ASTNode const &block = nullptr, std::wstring_view output = L""sv);
    BindResult bind(ASTNode const &n) const;
};

struct Continue : public AbstractSyntaxNode {
    Label label;

    Continue(Label label);
    BindResult bind(ASTNode const &n) const;
};

struct CString : public AbstractSyntaxNode {
    std::string string;
    CString(std::string string);
    BindResult bind(ASTNode const &n) const;
};

struct Decimal : public AbstractSyntaxNode {
    double value;

    Decimal(std::wstring_view whole, std::wstring_view fraction = L"", std::wstring_view exponent = L"");
    BindResult bind(ASTNode const &n) const;
};

struct DefaultSwitchValue : public AbstractSyntaxNode {
    DefaultSwitchValue() = default;
    BindResult bind(ASTNode const &n) const;
};

struct DeferStatement : public AbstractSyntaxNode {
    ASTNode statement;

    DeferStatement(ASTNode statement);
    BindResult bind(ASTNode const &n) const;
};

struct Dummy : public AbstractSyntaxNode {
    Dummy() = default;
    BindResult bind(ASTNode const &n) const;
};

struct Embed : public AbstractSyntaxNode {
    std::wstring file_name;

    Embed(std::wstring_view file_name);
};

struct EnumValue : public AbstractSyntaxNode {
    std::wstring label;
    ASTNode      value;
    ASTNode      payload;

    EnumValue(std::wstring label, ASTNode value, ASTNode payload);
    BindResult bind(ASTNode const &n) const;
};

struct Enum : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      underlying_type;
    ASTNodes     values;

    Enum(std::wstring name, ASTNode underlying_type, ASTNodes values);
    BindResult bind(ASTNode const &n) const;
};

struct ExportDeclaration : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      declaration;

    ExportDeclaration(std::wstring name, ASTNode declaration);
};

struct ExpressionList : public AbstractSyntaxNode {
    ASTNodes expressions;

    explicit ExpressionList(ASTNodes expressions);
    BindResult bind(ASTNode const &n) const;
};

struct Extern : public AbstractSyntaxNode {
    ASTNodes     declarations;
    std::wstring library;

    Extern(ASTNodes declarations, std::wstring library);
    BindResult bind(ASTNode const &n) const;
};

struct ExternLink : public AbstractSyntaxNode {
    std::wstring link_name;

    ExternLink(std::wstring link_name);
    BindResult bind(ASTNode const &n) const;
};

struct ForStatement : public AbstractSyntaxNode {
    std::wstring range_variable;
    ASTNode      range_expr;
    ASTNode      statement;
    Label        label;

    ForStatement(std::wstring var, ASTNode expr, ASTNode statement, Label label = { });
    BindResult bind(ASTNode const &n) const;
};

struct FunctionDeclaration : public AbstractSyntaxNode {
    std::wstring name;
    ASTNodes     generics;
    ASTNodes     parameters;
    ASTNode      return_type;

    FunctionDeclaration(std::wstring name, ASTNodes generics, ASTNodes parameters, ASTNode return_type);
    BindResult bind(ASTNode const &n) const;
};

struct FunctionDefinition : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      declaration;
    ASTNode      implementation;
    Visibility   visibility { Visibility::Static };

    FunctionDefinition(std::wstring name, ASTNode declaration, ASTNode implementation);
    FunctionDefinition(std::wstring name);
    ASTNode      instantiate(ASTNode const &n, std::vector<pType> const &generic_args) const;
    ASTNode      instantiate(ASTNode const &n, std::map<std::wstring, pType> const &generic_args) const;
    std::wstring mangled_name(ASTNode const &n) const;
    BindResult   bind(ASTNode const &n) const;
};

struct Identifier : public AbstractSyntaxNode {
    std::wstring identifier;

    explicit Identifier(std::wstring_view identifier);
    BindResult bind(ASTNode const &n) const;
};

struct IdentifierList : public AbstractSyntaxNode {
    Strings identifiers;

    IdentifierList(auto const &id)
    {
        emplace(id);
    }

    IdentifierList(auto const &id1, auto const &id2)
    {
        emplace(id1);
        emplace(id2);
    }

private:
    void emplace(std::wstring const &id)
    {
        identifiers.emplace_back(id);
    }

    void emplace(Strings const &ids)
    {
        for (std::wstring const &id : ids) {
            emplace(id);
        };
    }

    void emplace(IdentifierList const &ids)
    {
        emplace(ids.identifiers);
    }
};

struct IfStatement : public AbstractSyntaxNode {
    ASTNode condition;
    ASTNode if_branch;
    ASTNode else_branch;
    Label   label;

    IfStatement(ASTNode condition, ASTNode if_branch, ASTNode else_branch, Label label = { });
    BindResult bind(ASTNode const &n) const;
};

struct Import : public AbstractSyntaxNode {
    Strings file_name;

    explicit Import(Strings file_name);
};

struct Include : public AbstractSyntaxNode {
    std::wstring file_name;

    explicit Include(std::wstring_view file_name);
};

struct LoopStatement : public AbstractSyntaxNode {
    Label   label;
    ASTNode statement;

    LoopStatement(Label label, ASTNode statement);
    BindResult bind(ASTNode const &n) const;
};

struct Module : public AbstractSyntaxNode {
    std::wstring name;
    std::wstring source;
    ASTNodes     statements;

    Module(std::wstring name, std::wstring source);
    Module(std::wstring name, std::wstring source, ASTNodes const &statements);
    BindResult bind(ASTNode const &n) const;
};

struct ModuleProxy : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      module;

    ModuleProxy(std::wstring name, ASTNode module = nullptr);
    BindResult bind(ASTNode const &n) const;
};

struct Nullptr : public AbstractSyntaxNode {
    Nullptr();
    BindResult bind(ASTNode const &n) const;
};

struct Number : public AbstractSyntaxNode {
    using Int = std::variant<uint64_t, int64_t, uint32_t, int32_t, uint16_t, int16_t, uint8_t, int8_t>;
    Int value;

    Number(std::wstring_view number, Radix radix);
    Number(Int value);

    Number(std::integral auto value)
        : value(value)
    {
    }

    Number(pType const &type, auto v)
    {
        assert(is<IntType>(type));
        assign(get<IntType>(type), v);
    }

    Number(IntType const &type, auto v)
    {
        assign(type, v);
    }
    BindResult bind(ASTNode const &n) const;

private:
    void assign(IntType const &type, auto v)
    {
        if (type.is_signed) {
            switch (type.width_bits) {
            case 8:
                value.emplace<int8_t>(v);
                break;
            case 16:
                value.emplace<int16_t>(v);
                break;
            case 32:
                value.emplace<int32_t>(v);
                break;
            case 64:
                value.emplace<int64_t>(v);
                break;
            }
        } else {
            switch (type.width_bits) {
            case 8:
                value.emplace<uint8_t>(v);
                break;
            case 16:
                value.emplace<uint16_t>(v);
                break;
            case 32:
                value.emplace<uint32_t>(v);
                break;
            case 64:
                value.emplace<uint64_t>(v);
                break;
            }
        }
    }
};

template<std::integral T>
T get(Number const &number)
{
    return std::visit(
        [](auto v) -> T {
            return static_cast<T>(v);
        },
        number.value);
}

struct Parameter : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      type_name;

    Parameter(std::wstring name, ASTNode type_name);
    BindResult bind(ASTNode const &n) const;
};

struct Program : public AbstractSyntaxNode {
    std::wstring name;
    std::wstring source;
    ASTNodes     statements;

    Program(std::wstring name, std::wstring source);
    Program(std::wstring name, ASTNodes statements);
    Program(std::wstring name, std::wstring source, ASTNodes statements);
    BindResult bind(ASTNode const &n) const;
};

struct PublicDeclaration : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      declaration;

    PublicDeclaration(std::wstring name, ASTNode declaration);
    BindResult bind(ASTNode const &n) const;
};

struct QuotedString : public AbstractSyntaxNode {
    std::wstring string;
    QuoteType    quote_type;

    QuotedString(std::wstring_view str, QuoteType type);
};

struct Return : public AbstractSyntaxNode {
    ASTNode expression;

    explicit Return(ASTNode expression);
    BindResult bind(ASTNode const &n) const;
};

struct StampedIdentifier : public Identifier {
    ASTNodes arguments;

    explicit StampedIdentifier(std::wstring_view identifier, ASTNodes arguments);
};

struct String : public AbstractSyntaxNode {
    std::wstring string;
    String(std::wstring string);
    BindResult bind(ASTNode const &n) const;
};

struct StructMember : public AbstractSyntaxNode {
    std::wstring label;
    ASTNode      member_type;

    StructMember(std::wstring label, ASTNode payload);
    BindResult bind(ASTNode const &n) const;
};

struct Struct : public AbstractSyntaxNode {
    std::wstring name;
    ASTNodes     members;

    Struct(std::wstring name, ASTNodes members);
    BindResult bind(ASTNode const &n) const;
};

struct SwitchCase : public AbstractSyntaxNode {
    ASTNode case_value;
    ASTNode binding;
    ASTNode statement;

    SwitchCase(ASTNode case_value, ASTNode binding, ASTNode statement);
    BindResult bind(ASTNode const &n) const;
};

struct SwitchStatement : public AbstractSyntaxNode {
    Label    label;
    ASTNode  switch_value;
    ASTNodes switch_cases;

    SwitchStatement(Label label, ASTNode switch_value, ASTNodes switch_cases);
    BindResult bind(ASTNode const &n) const;
};

struct TagValue : public AbstractSyntaxNode {
    ASTNode      operand { nullptr };
    int64_t      tag_value;
    std::wstring label;
    pType        payload_type;
    ASTNode      payload { nullptr };

    TagValue(int64_t tag_value, std::wstring label, pType payload_type, ASTNode payload);
    TagValue(ASTNode operand, int64_t tag_value, std::wstring label, pType payload_type, ASTNode payload);
};

struct TypeNameNode {
    Strings  name;
    ASTNodes arguments { };
};

struct ReferenceDescriptionNode {
    ASTNode referencing;
};

struct SliceDescriptionNode {
    ASTNode slice_of;
};

struct ZeroTerminatedArrayDescriptionNode {
    ASTNode array_of;
};

struct ArrayDescriptionNode {
    ASTNode array_of;
    size_t  size;
};

struct DynArrayDescriptionNode {
    ASTNode array_of;
};

struct OptionalDescriptionNode {
    ASTNode optional_of;
};

struct PointerDescriptionNode {
    ASTNode referencing;
};

struct ResultDescriptionNode {
    ASTNode success;
    ASTNode error;
};

using TypeSpecificationDescription = std::variant<
    TypeNameNode,
    ReferenceDescriptionNode,
    SliceDescriptionNode,
    ZeroTerminatedArrayDescriptionNode,
    ArrayDescriptionNode,
    DynArrayDescriptionNode,
    OptionalDescriptionNode,
    PointerDescriptionNode,
    ResultDescriptionNode>;

template<class S>
concept is_type_specification = std::is_same_v<S, TypeNameNode>
    || std::is_same_v<S, ReferenceDescriptionNode>
    || std::is_same_v<S, SliceDescriptionNode>
    || std::is_same_v<S, ZeroTerminatedArrayDescriptionNode>
    || std::is_same_v<S, ArrayDescriptionNode>
    || std::is_same_v<S, DynArrayDescriptionNode>
    || std::is_same_v<S, OptionalDescriptionNode>
    || std::is_same_v<S, PointerDescriptionNode>
    || std::is_same_v<S, ResultDescriptionNode>;

struct TypeSpecification : public AbstractSyntaxNode {

    TypeSpecificationDescription description;

    TypeSpecification(TypeSpecificationDescription description);

    template<class S>
        requires is_type_specification<S>
    TypeSpecification(S specification)
        : description(specification)
    {
    }
    BindResult bind(ASTNode const &n) const;
};

struct UnaryExpression : public AbstractSyntaxNode {
    Operator op;
    ASTNode  operand;

    UnaryExpression(Operator op, ASTNode operand);
    BindResult bind(ASTNode const &n) const;
};

struct VariableDeclaration : public AbstractSyntaxNode {
    std::wstring name;
    ASTNode      type_name { };
    ASTNode      initializer;
    bool         is_const;
    Visibility   visibility { Visibility::Static };

    VariableDeclaration(std::wstring name, ASTNode type_name, ASTNode initializer, bool is_const);
    BindResult bind(ASTNode const &n) const;
};

struct Void : public AbstractSyntaxNode {
    Void();
    BindResult bind(ASTNode const &n) const;
};

struct WhileStatement : public AbstractSyntaxNode {
    Label   label;
    ASTNode condition;
    ASTNode statement;

    WhileStatement(Label label, ASTNode condition, ASTNode statement);
    BindResult bind(ASTNode const &n) const;
};

struct Yield : public AbstractSyntaxNode {
    Label   label;
    ASTNode statement;

    Yield(Label label, ASTNode statement);
    BindResult bind(ASTNode const &n) const;
};

template<class N>
concept is_component = std::is_same_v<N, Program> || std::is_same_v<N, Module>;

template<class N>
concept is_identifier = std::is_same_v<N, Identifier> || std::is_same_v<N, StampedIdentifier>;

template<typename N>
concept Constant = std::is_same_v<N, Number>
    || std::is_same_v<N, Decimal>
    || std::is_same_v<N, BoolConstant>
    || std::is_same_v<N, QuotedString>
    || std::is_same_v<N, TagValue>
    || std::is_same_v<N, Void>;

bool is_constant(ASTNode const &n);

template<typename N>
concept Breakable = std::is_same_v<N, Block>
    || std::is_same_v<N, IfStatement>
    || std::is_same_v<N, ForStatement>
    || std::is_same_v<N, LoopStatement>
    || std::is_same_v<N, SwitchStatement>
    || std::is_same_v<N, WhileStatement>;

template<typename N>
concept Continuable = std::is_same_v<N, ForStatement>
    || std::is_same_v<N, LoopStatement>
    || std::is_same_v<N, WhileStatement>;

using SyntaxNode = std::variant<Dummy,
    Alias,
    ArgumentList,
    BinaryExpression,
    Block,
    BoolConstant,
    Break,
    Call,
    Comptime,
    Continue,
    CString,
    Decimal,
    DefaultSwitchValue,
    DeferStatement,
    Embed,
    Enum,
    EnumValue,
    ExportDeclaration,
    ExpressionList,
    Extern,
    ExternLink,
    ForStatement,
    FunctionDeclaration,
    FunctionDefinition,
    Identifier,
    IdentifierList,
    IfStatement,
    Include,
    Import,
    LoopStatement,
    Module,
    ModuleProxy,
    Nullptr,
    Number,
    Parameter,
    Program,
    PublicDeclaration,
    QuotedString,
    Return,
    StampedIdentifier,
    String,
    Struct,
    StructMember,
    SwitchCase,
    SwitchStatement,
    TagValue,
    TypeSpecification,
    UnaryExpression,
    VariableDeclaration,
    Void,
    WhileStatement,
    Yield>;

struct ASTNodeImpl {
    using NodeTag = std::variant<bool, int64_t, std::wstring, pType, ASTNode>;
    TokenLocation location { };
    ASTStatus     status { ASTStatus::Initialized };
    SyntaxNode    node { };
    pType         bound_type { };
    NSNode        ns { nullptr };
    ASTNode       id;
    ASTNode       supercedes;
    ASTNode       superceded_by;
    NodeTag       tag { false };

    ASTNodeImpl(SyntaxNode impl)
        : node(std::move(impl))
    {
    }

    template<class N, typename... Args>
    static ASTNodeImpl make(TokenLocation const &loc, Args... args)
    {
        ASTNodeImpl ret = make<N>(loc, args...);
        ret.location = loc;
        return ret;
    }

    template<class N>
    static ASTNodeImpl make(N impl)
    {
        return ASTNodeImpl { SyntaxNode { std::move(impl) } };
    }

    template<class N, typename... Args>
    static ASTNodeImpl make(Args... args)
    {
        ASTNodeImpl ret;
        ret.node = N { args... };
        return ret;
    }

    template<typename N>
    static ASTNodeImpl make(SyntaxNode impl)
    {
        assert(std::holds_alternative<N>(impl));
        return { impl };
    }

    SyntaxNodeType type() const { return static_cast<SyntaxNodeType>(node.index()); }
    void           init_namespace();

private:
    ASTNodeImpl() = default;
};

template<class N>
N *get_if(ASTNode const &node)
{
    assert(node);
    return std::get_if<N>(&node->node);
}

template<class N>
N &get(ASTNode const &node)
{
    assert(node);
    return std::get<N>(node->node);
}

template<class N>
bool is(ASTNode const &node)
{
    assert(node);
    return std::holds_alternative<N>(node->node);
}

static inline std::wstring_view identifier(ASTNode const &n)
{
    return std::visit(
        overloads {
            [](Identifier const &node) -> std::wstring_view {
                return std::wstring_view(node.identifier);
            },
            [](StampedIdentifier const &node) -> std::wstring_view {
                return std::wstring_view(node.identifier);
            },
            [](auto const &node) -> std::wstring_view {
                UNREACHABLE();
            } },
        n->node);
}

static inline std::wstring_view name(ASTNode const &n)
{
    return std::visit(
        overloads {
            [](Enum const &node) -> std::wstring_view {
                return std::wstring_view(node.name);
            },
            [](FunctionDefinition const &node) -> std::wstring_view {
                return std::wstring_view(node.name);
            },
            [](FunctionDeclaration const &node) -> std::wstring_view {
                return std::wstring_view(node.name);
            },
            [](Module const &node) -> std::wstring_view {
                return std::wstring_view(node.name);
            },
            [](Program const &node) -> std::wstring_view {
                return std::wstring_view(node.name);
            },
            [](auto const &node) -> std::wstring_view {
                UNREACHABLE();
            } },
        n->node);
}

struct Operand {
    OperandType type;

    Operand(TypeKind k);
    Operand(PseudoType pseudo_type);
    bool matches(ASTNode const &node, pType const &concrete, pType const &hint = nullptr) const;
};

struct BinaryOperator {
    Operand     lhs;
    Operator    op;
    Operand     rhs;
    OperandType result;

    bool  matches(ASTNode const &node, pType const &concrete_lhs, pType const &concrete_rhs) const;
    pType return_type(ASTNode const &node, pType const &lhs_type, pType const &rhs_type) const;
};

struct AssignOperator {
    Operator assign_op;
    Operator bin_op;
};

struct UnaryOperator {
    Operator    op;
    Operand     operand;
    OperandType result;
};

extern std::vector<BinaryOperator>  binary_ops;
extern std::vector<UnaryOperator>   unary_ops;
extern std::map<Operator, Operator> assign_ops;

void         dump(ASTNode const &node, std::wostream &os, int indent = 0);
void         dump(ASTNodes const &nodes, std::wostream &os, int indent = 0);
void         header(ASTNode const &node, std::wostream &os);
std::wstring to_string(ASTNode const &node);
pType        resolve(ASTNode const &type_node);
ASTNode      normalize(ASTNode node);
ASTNodes     normalize(ASTNodes nodes);
BindResult   bind(ASTNode node);
ASTNode      coerce(ASTNode node, pType const &type);
ASTNode      stamp(ASTNode node);
ASTNodes     stamp(ASTNodes nodes);
ASTNode      fold(ASTNode node);

#define try_bind(expr)                                                      \
    (                                                                       \
        {                                                                   \
            auto const &__expr = (expr);                                    \
            if (auto const maybe = Lia::bind(__expr); !maybe.has_value()) { \
                return BindError { maybe.error() };                         \
            }                                                               \
            ((__expr)->bound_type);                                         \
        })

template<typename R>
    requires std::ranges::range<R> && std::same_as<std::ranges::range_value_t<R>, ASTNode>
BindResults bind_nodes(R const &nodes)
{
    pTypes                   types;
    std::optional<ASTStatus> ret { };

    for (auto &n : nodes) {
        auto res = bind(n);
        if (!res.has_value() || res.value() == nullptr) {
            if (!ret.has_value()) {
                if (!res.has_value()) {
                    ret = res.error();
                } else {
                    ret = ASTStatus::Undetermined;
                }
            }
            types.push_back(pType { nullptr });
        } else {
            types.push_back(res.value());
        }
    }
    if (ret.has_value()) {
        return std::unexpected(ret.value());
    }
    return types;
}

static BindResults bind_nodes(ASTNodes nodes)
{
    return bind_nodes(nodes | std::ranges::views::all);
}

#define try_bind_nodes(nodes)                                                 \
    (                                                                         \
        {                                                                     \
            auto const &__nodes = (nodes);                                    \
            pTypes      __types;                                              \
            if (auto const maybe = bind_nodes(__nodes); !maybe.has_value()) { \
                return BindError { maybe.error() };                           \
            } else {                                                          \
                __types = maybe.value();                                      \
            }                                                                 \
            (__types);                                                        \
        })

}

std::wostream &operator<<(std::wostream &os, Lia::SyntaxNode const &node);

namespace std {

using namespace Lia;

template<>
struct formatter<ASTNode, wchar_t> {
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
            throw std::format_error("Invalid format args for ASTNode");
        }
        ++it;
        if (it != ctx.end() && *it != '}') {
            throw std::format_error("Invalid format args for ASTNode");
        }
        return it;
    }

    template<class FmtContext>
    FmtContext::iterator format(ASTNode const &node, FmtContext &ctx) const
    {
        std::wostringstream out;
        header(node, out);
        if (with_type) {
            out << " [" << SyntaxNodeType_name(node->type()) << ']';
        }
        return std::ranges::copy(std::move(out).str(), ctx.out()).out;
    }
};

}
