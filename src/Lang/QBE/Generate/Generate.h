#pragma once

#include <Lang/QBE/QBE.h>

#include <Lang/Parser.h>

namespace Lang::QBE {

GenResult  generate_qbe_node(ASTNode const &, QBEContext &);
GenResult  generate_qbe_nodes(ASTNodes const &nodes, QBEContext &ctx);
GenResult  assign(QBEOperand const &lhs, ASTNode const &rhs, QBEContext &ctx);
GenResult  cast(QBEOperand value, pType const &target_type, QBEContext &ctx);
GenResult  variable_decl(ASTNode const &n, std::wstring const &name, pType const &type, ASTNode const &init, QBEContext &ctx);
GenResult  global_decl(ASTNode const &n, std::wstring const &name, pType const &type, ASTNode const &init, QBEContext &ctx);
GenBinExpr make_binexpr(ASTNode const &n, QBEOperand lhs, Operator op, QBEOperand rhs, QBEContext &ctx);

template<typename T>
T as(VM const &, QBEValue const &)
{
    std::unreachable();
}

template<class Node>
GenResult generate_qbe_node(ASTNode const &, Node const &impl, QBEContext &)
{
    NYI(L"Unimplemented QBE serialization for {}", demangle<decltype(impl)>());
}

template<>
GenResult generate_qbe_node(ASTNode const &n, Alias const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, ArgumentList const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, BinaryExpression const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Block const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Break const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Call const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &, CapturedPayload const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &, Comptime const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Void const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, BoolConstant const &impl, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, Number const &impl, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, Decimal const &impl, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, CString const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, String const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Dummy const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, Nullptr const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, DeferStatement const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Enum const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, ExpressionList const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Extern const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, FunctionDeclaration const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, FunctionDefinition const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, ForStatement const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Identifier const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, IfStatement const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, LoopStatement const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Module const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, ModuleProxy const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, Parameter const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Program const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &, PublicDeclaration const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Return const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Struct const &, QBEContext &);
template<>
GenResult generate_qbe_node(ASTNode const &n, SwitchStatement const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, Tag const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, UnaryExpression const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, VariableDeclaration const &impl, QBEContext &ctx);
template<>
GenResult generate_qbe_node(ASTNode const &n, WhileStatement const &impl, QBEContext &ctx);

}
