#pragma once
#include "../grammar/RxParser.h"
#include "AstNode.h"
#include "expr.h"
#include "item.h"
#include "stmt.h"
#include "type.h"
#include <string>
#include <utility>
#include <vector>

class AstBuilder {
public:
    explicit AstBuilder(std::string prog);
    NodePtr<Crate> build(RxParser::CrateContext *ctx);
    static     NodePtr<Crate> parse(const std::string &src);
    static NodePtr<AstNode> parseEntry(const std::string &src, const std::string &entry);

private:
    std::string program;

    std::string text(antlr4::tree::ParseTree *tree);
    std::string text(antlr4::Token *tok);
    // SourceSpan span(antlr4::ParserRuleContext *ctx);
    // SourceSpan spanFrom(antlr4::ParserRuleContext *first,
    //                     antlr4::ParserRuleContext *last);

    template <class T, class... A>
    NodePtr<T> make(SourceSpan s, A &&...args) {
        return std::make_unique<T>(s, std::forward<A>(args)...);
    }

    std::string ident(RxParser::IdentifierContext *ctx);
    IntValue parseInteger(antlr4::Token *tok);

    UnaryOp toUnaryOp(RxParser::UnaryOperatorContext *ctx);
    BinaryOp toBinaryOp(RxParser::MultiplicativeOperatorContext *ctx);
    BinaryOp toBinaryOp(RxParser::AdditiveOperatorContext *ctx);
    BinaryOp toBinaryOp(RxParser::ComparisonExceptLtContext *ctx);
    AssignOp toAssignOp(RxParser::AssignmentOperatorContext *ctx);
    Derive toDerive(RxParser::DeriveNameContext *ctx);

    NodePtr<Item> buildItem(RxParser::ItemContext *ctx);
    NodePtr<Item> buildUseDeclaration(RxParser::UseDeclarationContext *ctx);
    NodePtr<Item> buildFunctionDefinition(RxParser::FunctionDefinitionContext *ctx);
    std::vector<NodePtr<FuncParam>> buildFunctionParameters(
        RxParser::FunctionParametersContext *ctx);
    NodePtr<Item> buildStructDefinition(RxParser::StructDefinitionContext *ctx);
    NodePtr<StructField> buildStructField(RxParser::StructFieldContext *ctx);
    NodePtr<Item> buildConstantItem(RxParser::ConstantItemContext *ctx);
    NodePtr<Item> buildInherentImpl(RxParser::InherentImplContext *ctx);
    NodePtr<Item> buildAssociatedItem(RxParser::AssociatedItemContext *ctx);

    NodePtr<Type> buildTypeRef(RxParser::TypeRefContext *ctx);
    NodePtr<Type> buildReferenceType(RxParser::ReferenceTypeContext *ctx);
    NodePtr<Type> buildArrayType(RxParser::ArrayTypeContext *ctx);
    NodePtr<Type> buildTypePath(RxParser::TypePathContext *ctx);
    NodePtr<Type> buildClosedCastType(RxParser::ClosedCastTypeContext *ctx);
    NodePtr<Type> buildGenericArg(RxParser::GenericArgContext *ctx);

    NodePtr<PathExpr> buildPathInExpression(RxParser::PathInExpressionContext *ctx);
    PathSegment buildPathIdentSegment(RxParser::PathIdentSegmentContext *ctx);
    PathSegment buildPathExprSegment(RxParser::PathExprSegmentContext *ctx);
    PathSegment buildTypePathSegment(RxParser::TypePathSegmentContext *ctx);
    std::vector<NodePtr<Type>> buildGenericArgs(RxParser::GenericArgsContext *ctx);

    NodePtr<Expr> buildConstValue(RxParser::ConstValueContext *ctx);
    NodePtr<Expr> buildMagnitude(RxParser::MagnitudeContext *ctx);
    NodePtr<FuncParam> buildSelfParam(RxParser::SelfParamContext *ctx);
    NodePtr<FuncParam> buildFunctionParam(RxParser::FunctionParamContext *ctx);
    std::pair<std::string, bool> buildIdentifierBinding(RxParser::IdentifierBindingContext *ctx);
    std::vector<Derive> buildDerives(std::vector<RxParser::OuterAttributeContext *> ctxs);

    NodePtr<Expr> binary(BinaryOp op, NodePtr<Expr> lhs, NodePtr<Expr> rhs);

    NodePtr<Expr> buildExpr(RxParser::ExpressionContext *ctx);
    NodePtr<Expr> buildAssignmentExpr(RxParser::AssignmentExpressionContext *ctx);
    NodePtr<Expr> buildLogicalOrExpr(RxParser::LogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildLogicalAndExpr(RxParser::LogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildComparisonExpr(RxParser::ComparisonExpressionContext *ctx);
    NodePtr<Expr> buildBitOrExpr(RxParser::BitOrExpressionContext *ctx);
    NodePtr<Expr> buildClosedBitOrExpr(RxParser::ClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildBitXorExpr(RxParser::BitXorExpressionContext *ctx);
    NodePtr<Expr> buildClosedBitXorExpr(RxParser::ClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildBitAndExpr(RxParser::BitAndExpressionContext *ctx);
    NodePtr<Expr> buildClosedBitAndExpr(RxParser::ClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildShiftExpr(RxParser::ShiftExpressionContext *ctx);
    NodePtr<Expr> buildClosedShiftExpr(RxParser::ClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildAdditiveExpr(RxParser::AdditiveExpressionContext *ctx);
    NodePtr<Expr> buildClosedAdditiveExpr(RxParser::ClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildMultiplicativeExpr(RxParser::MultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildClosedMultiplicativeExpr(RxParser::ClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildCastExpr(RxParser::CastExpressionContext *ctx);
    NodePtr<Expr> buildClosedCastExpr(RxParser::ClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildUnaryExpr(RxParser::UnaryExpressionContext *ctx);
    NodePtr<Expr> buildPostfixExpr(RxParser::PostfixExpressionContext *ctx);

    NodePtr<Expr> buildConditionExpr(RxParser::ConditionExpressionContext *ctx);
    NodePtr<Expr> buildConditionAssignmentExpr(RxParser::ConditionAssignmentExpressionContext *ctx);
    NodePtr<Expr> buildConditionLogicalOrExpr(RxParser::ConditionLogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildConditionLogicalAndExpr(RxParser::ConditionLogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildConditionComparisonExpr(RxParser::ConditionComparisonExpressionContext *ctx);
    NodePtr<Expr> buildConditionBitOrExpr(RxParser::ConditionBitOrExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedBitOrExpr(RxParser::ConditionClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildConditionBitXorExpr(RxParser::ConditionBitXorExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedBitXorExpr(RxParser::ConditionClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildConditionBitAndExpr(RxParser::ConditionBitAndExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedBitAndExpr(RxParser::ConditionClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildConditionShiftExpr(RxParser::ConditionShiftExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedShiftExpr(RxParser::ConditionClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildConditionAdditiveExpr(RxParser::ConditionAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedAdditiveExpr(RxParser::ConditionClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildConditionMultiplicativeExpr(RxParser::ConditionMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedMultiplicativeExpr(RxParser::ConditionClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildConditionCastExpr(RxParser::ConditionCastExpressionContext *ctx);
    NodePtr<Expr> buildConditionClosedCastExpr(RxParser::ConditionClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildConditionUnaryExpr(RxParser::ConditionUnaryExpressionContext *ctx);
    NodePtr<Expr> buildConditionPostfixExpr(RxParser::ConditionPostfixExpressionContext *ctx);

    NodePtr<Expr> buildConditionBreakExpr(RxParser::ConditionBreakExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakAssignmentExpr(RxParser::ConditionBreakAssignmentExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakLogicalOrExpr(RxParser::ConditionBreakLogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakLogicalAndExpr(RxParser::ConditionBreakLogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakComparisonExpr(RxParser::ConditionBreakComparisonExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakBitOrExpr(RxParser::ConditionBreakBitOrExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedBitOrExpr(RxParser::ConditionBreakClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakBitXorExpr(RxParser::ConditionBreakBitXorExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedBitXorExpr(RxParser::ConditionBreakClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakBitAndExpr(RxParser::ConditionBreakBitAndExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedBitAndExpr(RxParser::ConditionBreakClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakShiftExpr(RxParser::ConditionBreakShiftExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedShiftExpr(RxParser::ConditionBreakClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakAdditiveExpr(RxParser::ConditionBreakAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedAdditiveExpr(RxParser::ConditionBreakClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakMultiplicativeExpr(RxParser::ConditionBreakMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedMultiplicativeExpr(RxParser::ConditionBreakClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakCastExpr(RxParser::ConditionBreakCastExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakClosedCastExpr(RxParser::ConditionBreakClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakUnaryExpr(RxParser::ConditionBreakUnaryExpressionContext *ctx);
    NodePtr<Expr> buildConditionBreakPostfixExpr(RxParser::ConditionBreakPostfixExpressionContext *ctx);

    NodePtr<Expr> buildStatementExpr(RxParser::StatementExpressionContext *ctx);
    NodePtr<Expr> buildStatementAssignmentExpr(RxParser::StatementAssignmentExpressionContext *ctx);
    NodePtr<Expr> buildStatementLogicalOrExpr(RxParser::StatementLogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildStatementLogicalAndExpr(RxParser::StatementLogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildStatementComparisonExpr(RxParser::StatementComparisonExpressionContext *ctx);
    NodePtr<Expr> buildStatementBitOrExpr(RxParser::StatementBitOrExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedBitOrExpr(RxParser::StatementClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildStatementBitXorExpr(RxParser::StatementBitXorExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedBitXorExpr(RxParser::StatementClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildStatementBitAndExpr(RxParser::StatementBitAndExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedBitAndExpr(RxParser::StatementClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildStatementShiftExpr(RxParser::StatementShiftExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedShiftExpr(RxParser::StatementClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildStatementAdditiveExpr(RxParser::StatementAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedAdditiveExpr(RxParser::StatementClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildStatementMultiplicativeExpr(RxParser::StatementMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedMultiplicativeExpr(RxParser::StatementClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildStatementCastExpr(RxParser::StatementCastExpressionContext *ctx);
    NodePtr<Expr> buildStatementClosedCastExpr(RxParser::StatementClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildStatementUnaryExpr(RxParser::StatementUnaryExpressionContext *ctx);
    NodePtr<Expr> buildStatementPostfixExpr(RxParser::StatementPostfixExpressionContext *ctx);

    NodePtr<Expr> buildPrimaryExpr(RxParser::PrimaryExpressionContext *ctx);
    NodePtr<Expr> buildNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx);
    NodePtr<Expr> buildConditionPrimary(RxParser::ConditionPrimaryContext *ctx);
    NodePtr<Expr> buildConditionPrimaryWithoutBareBlock(
        RxParser::ConditionPrimaryWithoutBareBlockContext *ctx);

    NodePtr<Stmt> buildStatement(RxParser::StatementContext *ctx);
    NodePtr<Stmt> buildLetStatement(RxParser::LetStatementContext *ctx);
    NodePtr<BlockExpr> buildBlockExpr(RxParser::BlockExpressionContext *ctx);
    NodePtr<Expr> buildExpressionWithBlock(RxParser::ExpressionWithBlockContext *ctx);
    NodePtr<Expr> buildIfExpr(RxParser::IfExpressionContext *ctx);

    NodePtr<Expr> buildLiteralExpr(RxParser::LiteralExpressionContext *ctx);
    NodePtr<Expr> buildArrayExpr(RxParser::ArrayExpressionContext *ctx);
    std::vector<NodePtr<StructExprField>> buildStructExprFields(
        RxParser::StructExprFieldsContext *ctx);
    NodePtr<StructExprField> buildStructExprField(RxParser::StructExprFieldContext *ctx);
};
