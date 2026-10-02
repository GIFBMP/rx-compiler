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

struct Error {
    SourceSpan span;
    std::string message;
};

class AstBuilder {
public:
    explicit AstBuilder(std::string prog);
    NodePtr<Crate> build(RxParser::CrateContext *ctx);
    static NodePtr<Crate> parse(const std::string &src);

    bool hasError() const;
    const std::vector<Error> &errors() const;

private:
    std::string prog;
    std::vector<Error> errs;

    std::string text(antlr4::tree::ParseTree *tree);
    std::string text(antlr4::Token *tok);
    std::string rawSlice(antlr4::Token *begin, antlr4::Token *stop);
    SourceSpan span(antlr4::ParserRuleContext *ctx);
    SourceSpan spanFrom(antlr4::ParserRuleContext *first,
                        antlr4::ParserRuleContext *last);

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

    void error(SourceSpan s, std::string msg);

    NodePtr<Type> buildClosedCastType(RxParser::ClosedCastTypeContext *ctx);

    PathSegment buildPathSegment(RxParser::PathExprSegmentContext *ctx);
    PathSegment buildTypePathSegment(RxParser::TypePathSegmentContext *ctx);
    std::vector<NodePtr<Type>> buildGenericArgs(RxParser::GenericArgsContext *ctx);

    NodePtr<Expr> buildConstValue(RxParser::ConstValueContext *ctx);
    NodePtr<Expr> buildMagnitude(RxParser::MagnitudeContext *ctx);
    NodePtr<FuncParam> buildSelfParam(RxParser::SelfParamContext *ctx);
    NodePtr<FuncParam> buildFunctionParam(RxParser::FunctionParamContext *ctx);
    std::pair<std::string, bool> buildIdentifierBinding(RxParser::IdentifierBindingContext *ctx);
    std::vector<Derive> buildDerives(std::vector<RxParser::OuterAttributeContext *> ctxs);

    NodePtr<Expr> applyPostfixSuffix(NodePtr<Expr> base, RxParser::PostfixSuffixContext *ctx);
    NodePtr<Expr> buildDotSuffix(NodePtr<Expr> base, RxParser::DotSuffixContext *ctx);
    std::vector<NodePtr<Expr>> buildCallArguments(RxParser::CallArgumentsContext *ctx);
    void ignoreGenericParams(RxParser::GenericParamsContext *ctx);
    void ignoreWhereClause(RxParser::WhereClauseContext *ctx);

    NodePtr<Expr> buildExpr(RxParser::ExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::AssignmentExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::LogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::LogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ComparisonExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::BitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::BitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::BitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::AdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::MultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::CastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::UnaryExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::PostfixExpressionContext *ctx);

    NodePtr<Expr> buildExpr(RxParser::ConditionExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionAssignmentExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionLogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionLogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionComparisonExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionUnaryExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionPostfixExpressionContext *ctx);

    NodePtr<Expr> buildExpr(RxParser::ConditionBreakExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakAssignmentExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakLogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakLogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakComparisonExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakUnaryExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::ConditionBreakPostfixExpressionContext *ctx);

    NodePtr<Expr> buildExpr(RxParser::StatementExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementAssignmentExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementLogicalOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementLogicalAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementComparisonExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedBitOrExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedBitXorExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedBitAndExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedShiftExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedAdditiveExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedMultiplicativeExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementClosedCastExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementUnaryExpressionContext *ctx);
    NodePtr<Expr> buildExpr(RxParser::StatementPostfixExpressionContext *ctx);

    NodePtr<Expr> buildPrimary(RxParser::PrimaryExpressionContext *ctx);
    NodePtr<Expr> buildNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx);
    NodePtr<Expr> buildConditionPrimary(RxParser::ConditionPrimaryContext *ctx);
    NodePtr<Expr> buildConditionPrimaryWithoutBareBlock(
        RxParser::ConditionPrimaryWithoutBareBlockContext *ctx);
};
