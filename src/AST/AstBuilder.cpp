#include "AstBuilder.h"
#include <exception>

UnaryOp AstBuilder::toUnaryOp(RxParser::UnaryOperatorContext *ctx) {
    if(ctx->AMP() && ctx->MUT()) return UnaryOp::RefMut;
    else if(ctx->MINUS()) return UnaryOp::Neg;
    else if(ctx->NOT()) return UnaryOp::Not;
    else if(ctx->STAR()) return UnaryOp::Deref;
    else if(ctx->AMP()) return UnaryOp::Ref;
    //ANDAND() should be processed in NodePtr<Expr> buildUnaryExpr(RxParser::UnaryExpressionContext *ctx);
    throw std::logic_error("Unexpected Unary Operator");
}

BinaryOp AstBuilder::toBinaryOp(RxParser::MultiplicativeOperatorContext *ctx) {
    if(ctx->STAR()) return BinaryOp::Mul;
    else if(ctx->SLASH()) return BinaryOp::Div;
    else if(ctx->PERCENT()) return BinaryOp::Mod;
    throw std::logic_error("Unexpected Binary Multiplicative Operator");
}

BinaryOp AstBuilder::toBinaryOp(RxParser::AdditiveOperatorContext *ctx) {
    if(ctx->PLUS()) return BinaryOp::Add;
    else if(ctx->MINUS()) return BinaryOp::Sub;
    throw std::logic_error("Unexpected Binary Additive Operator");
}

BinaryOp AstBuilder::toBinaryOp(RxParser::ComparisonExceptLtContext *ctx) {
    if(ctx->EQEQ()) return BinaryOp::Eq;
    else if(ctx->NE()) return BinaryOp::Ne;
    else if(ctx->LE()) return BinaryOp::Le;
    else if(ctx->GT() && ctx->GE_EQ()) return BinaryOp::Ge;
    else if(ctx->GT_SECOND() && ctx->SHR_EQ()) return BinaryOp::Ge;
    else if(ctx->genericClose()) return BinaryOp::Gt;
    throw std::logic_error("Unexpected Binary Comparison Operator");
}

AssignOp AstBuilder::toAssignOp(RxParser::AssignmentOperatorContext *ctx) {
    if(ctx->equalsSign()) return AssignOp::Assign;
    else if(ctx->PLUS_ASSIGN()) return AssignOp::Add;
    else if(ctx->MINUS_ASSIGN()) return AssignOp::Sub;
    else if(ctx->STAR_ASSIGN()) return AssignOp::Mul;
    else if(ctx->SLASH_ASSIGN()) return AssignOp::Div;
    else if(ctx->PERCENT_ASSIGN()) return AssignOp::Mod;
    else if(ctx->AMP_ASSIGN()) return AssignOp::BitAnd;
    else if(ctx->PIPE_ASSIGN()) return AssignOp::BitOr;
    else if(ctx->CARET_ASSIGN()) return AssignOp::BitXor;
    else if(ctx->SHL_ASSIGN()) return AssignOp::Shl;
    else if(ctx->GT() && ctx->GT_SECOND() && ctx->SHR_EQ()) return AssignOp::Shr;
    throw std::logic_error("Unexpected Assignment Operator");
}

Derive AstBuilder::toDerive(RxParser::DeriveNameContext *ctx) {
    if(ctx->COPY()) return Derive::Copy;
    else if(ctx->CLONE()) return Derive::Clone;
    else if(ctx->PARTIAL_EQ()) return Derive::PartialEq;
    else if(ctx->EQ()) return Derive::Eq;
    throw std::logic_error("Unexpected Derive Name");
}

NodePtr<Expr> AstBuilder::binary(BinaryOp op, NodePtr<Expr> lhs, NodePtr<Expr> rhs) {
    SourceSpan span{lhs->span.beginLine, lhs->span.beginCol,
                    rhs->span.endLine, rhs->span.endCol};
    auto ret = std::make_unique<BinaryExpr>(span);
    ret->lhs = std::move(lhs);
    ret->rhs = std::move(rhs);
    ret->op = op;
    return ret;
}

NodePtr<Expr> AstBuilder::buildExpr(RxParser::ExpressionContext *ctx) {
    return buildAssignmentExpr(ctx->assignmentExpression());
}

NodePtr<Expr> AstBuilder::buildAssignmentExpr(RxParser::AssignmentExpressionContext *ctx) {
    if(!ctx->assignmentOperator()) {
        return buildLogicalOrExpr(ctx->logicalOrExpression());
    }
    auto ret = std::make_unique<AssignExpr>(spanOf(ctx));
    ret->lhs = buildLogicalOrExpr(ctx->logicalOrExpression());
    ret->op = toAssignOp(ctx->assignmentOperator());
    ret->rhs = buildExpr(ctx->expression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildLogicalOrExpr(RxParser::LogicalOrExpressionContext *ctx) {
    const auto operands = ctx->logicalAndExpression();
    auto lhs = buildLogicalAndExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::LogicalOr, std::move(lhs), buildLogicalAndExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildLogicalAndExpr(RxParser::LogicalAndExpressionContext *ctx) {
    const auto operands = ctx->comparisonExpression();
    auto lhs = buildComparisonExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::LogicalAnd, std::move(lhs), buildComparisonExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildComparisonExpr(RxParser::ComparisonExpressionContext *ctx) {
    if(!ctx->comparisonExceptLt() && !ctx->LT()) {
        return buildBitOrExpr(ctx->bitOrExpression()[0]);
    }
    if(ctx->LT()) {
        return binary(BinaryOp::Lt,
                      buildClosedBitOrExpr(ctx->closedBitOrExpression()),
                      buildBitOrExpr(ctx->bitOrExpression()[0]));
    }
    return binary(toBinaryOp(ctx->comparisonExceptLt()),
                  buildBitOrExpr(ctx->bitOrExpression()[0]),
                  buildBitOrExpr(ctx->bitOrExpression()[1]));
}

NodePtr<Expr> AstBuilder::buildBitOrExpr(RxParser::BitOrExpressionContext *ctx) {
    const auto operands = ctx->bitXorExpression();
    auto lhs = buildBitXorExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), buildBitXorExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildClosedBitOrExpr(RxParser::ClosedBitOrExpressionContext *ctx) {
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->bitXorExpression()) {
        operands.push_back(buildBitXorExpr(operand));
    }
    operands.push_back(buildClosedBitXorExpr(ctx->closedBitXorExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildBitXorExpr(RxParser::BitXorExpressionContext *ctx) {
    const auto operands = ctx->bitAndExpression();
    auto lhs = buildBitAndExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), buildBitAndExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildClosedBitXorExpr(RxParser::ClosedBitXorExpressionContext *ctx) {
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->bitAndExpression()) {
        operands.push_back(buildBitAndExpr(operand));
    }
    operands.push_back(buildClosedBitAndExpr(ctx->closedBitAndExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildBitAndExpr(RxParser::BitAndExpressionContext *ctx) {
    const auto operands = ctx->shiftExpression();
    auto lhs = buildShiftExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), buildShiftExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildClosedBitAndExpr(RxParser::ClosedBitAndExpressionContext *ctx) {
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->shiftExpression()) {
        operands.push_back(buildShiftExpr(operand));
    }
    operands.push_back(buildClosedShiftExpr(ctx->closedShiftExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildShiftExpr(RxParser::ShiftExpressionContext *ctx) {
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *closed = dynamic_cast<RxParser::ClosedAdditiveExpressionContext *>(child)) {
            return buildClosedAdditiveExpr(closed);
        }
        return buildAdditiveExpr(dynamic_cast<RxParser::AdditiveExpressionContext *>(child));
    };
    const auto &kids = ctx->children;
    auto lhs = operandAt(kids[0]);
    for(size_t i = 1; i + 1 < kids.size(); i += 2) {
        BinaryOp op = dynamic_cast<RxParser::ShiftRightContext *>(kids[i])
                          ? BinaryOp::Shr : BinaryOp::Shl;
        lhs = binary(op, std::move(lhs), operandAt(kids[i + 1]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildClosedShiftExpr(RxParser::ClosedShiftExpressionContext *ctx) {
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *closed = dynamic_cast<RxParser::ClosedAdditiveExpressionContext *>(child)) {
            return buildClosedAdditiveExpr(closed);
        }
        return buildAdditiveExpr(dynamic_cast<RxParser::AdditiveExpressionContext *>(child));
    };
    const auto &kids = ctx->children;
    auto lhs = operandAt(kids[0]);
    for(size_t i = 1; i + 1 < kids.size(); i += 2) {
        BinaryOp op = dynamic_cast<RxParser::ShiftRightContext *>(kids[i])
                          ? BinaryOp::Shr : BinaryOp::Shl;
        lhs = binary(op, std::move(lhs), operandAt(kids[i + 1]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildAdditiveExpr(RxParser::AdditiveExpressionContext *ctx) {
    const auto operands = ctx->multiplicativeExpression();
    const auto ops = ctx->additiveOperator();
    auto lhs = buildMultiplicativeExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), buildMultiplicativeExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildClosedAdditiveExpr(RxParser::ClosedAdditiveExpressionContext *ctx) {
    const auto ops = ctx->additiveOperator();
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->multiplicativeExpression()) {
        operands.push_back(buildMultiplicativeExpr(operand));
    }
    operands.push_back(buildClosedMultiplicativeExpr(ctx->closedMultiplicativeExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildMultiplicativeExpr(RxParser::MultiplicativeExpressionContext *ctx) {
    const auto operands = ctx->castExpression();
    const auto ops = ctx->multiplicativeOperator();
    auto lhs = buildCastExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), buildCastExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildClosedMultiplicativeExpr(RxParser::ClosedMultiplicativeExpressionContext *ctx) {
    const auto ops = ctx->multiplicativeOperator();
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->castExpression()) {
        operands.push_back(buildCastExpr(operand));
    }
    operands.push_back(buildClosedCastExpr(ctx->closedCastExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Type> AstBuilder::buildTypeRef(RxParser::TypeRefContext* ctx) {
    if(ctx->LPAREN() && ctx->RPAREN()) {
        if(ctx->typeRef()) {
            auto ret = std::make_unique<ParenthesizedType>(spanOf(ctx));
            ret->inner = buildTypeRef(ctx->typeRef());
            return ret;
        }
        else {//Unit Type? ()
            return std::make_unique<UnitType>(spanOf(ctx));
        }
    }
    else if(ctx->typePath()) {
        return buildTypePath(ctx->typePath());
    }
    else if(ctx->referenceType()) {
        return buildReferenceType(ctx->referenceType());
    }
    else if(ctx->arrayType()) {
        return buildArrayType(ctx->arrayType());
    }
    throw std::logic_error("Invalid TypeRef");
}

NodePtr<Expr> AstBuilder::buildCastExpr(RxParser::CastExpressionContext *ctx) {
    auto lhs = buildUnaryExpr(ctx->unaryExpression());
    const auto &typ = ctx->typeRef();
    for(size_t i = 0; i < typ.size(); i++) {
        auto rhs = buildTypeRef(typ[i]);
        SourceSpan span;
        span.beginCol = lhs->span.beginCol;
        span.beginLine = lhs->span.beginLine;
        span.endCol = rhs->span.endCol;
        span.endLine = rhs->span.endLine;
        auto ret = std::make_unique<CastExpr>(span);
        ret->expr = std::move(lhs);
        ret->type = std::move(rhs);
        lhs = std::move(ret);
    }
    return lhs;
}

PathSegment AstBuilder::buildPathIdentSegment(RxParser::PathIdentSegmentContext *ctx) {
    PathSegment seg;
    if(ctx->identifier()) {
        seg.ident = ident(ctx->identifier());
    }
    else if(ctx->SELF_VALUE()) {
        seg.ident = "self";
        seg.isSelf = true;
    }
    else if(ctx->SELF_TYPE()) {
        seg.ident = "Self";
        seg.isSelfType = true;
    }
    return seg;
}

PathSegment AstBuilder::buildTypePathSegment(RxParser::TypePathSegmentContext *ctx) {
    PathSegment seg = buildPathIdentSegment(ctx->pathIdentSegment());
    if(ctx->genericArgs()) {
        seg.typeArgs = buildGenericArgs(ctx->genericArgs());
    }
    return seg;
}

NodePtr<Type> AstBuilder::buildGenericArg(RxParser::GenericArgContext *ctx) {
    if(ctx->typeRef()) {
        return buildTypeRef(ctx->typeRef());
    }
    return nullptr;
}

std::vector<NodePtr<Type>> AstBuilder::buildGenericArgs(RxParser::GenericArgsContext *ctx) {
    std::vector<NodePtr<Type>> args;
    for(auto *arg : ctx->genericArg()) {
        auto type = buildGenericArg(arg);
        if(type) {
            args.push_back(std::move(type));
        }
    }
    return args;
}

NodePtr<Type> AstBuilder::buildClosedCastType(RxParser::ClosedCastTypeContext *ctx) {
    if(ctx->LPAREN() && ctx->RPAREN()) {
        if(ctx->typeRef()) {
            auto ret = std::make_unique<ParenthesizedType>(spanOf(ctx));
            ret->inner = buildTypeRef(ctx->typeRef());
            return ret;
        }
        else {//Unit Type? ()
            return std::make_unique<UnitType>(spanOf(ctx));
        }
    }
    else if(ctx->arrayType()) {
        return buildArrayType(ctx->arrayType());
    }
    else if(ctx->closedCastType()) {
        if(ctx->AMP()) {//& ref type
            auto ret = std::make_unique<RefType>(spanOf(ctx));
            ret->isMut = ctx->MUT() ? true : false;
            ret->referent = buildClosedCastType(ctx->closedCastType());
            return ret;
        }
        else if(ctx->ANDAND()) {
            auto innernode = std::make_unique<RefType>(spanOf(ctx));
            innernode->isMut = ctx->MUT() ? true : false;
            innernode->referent = buildClosedCastType(ctx->closedCastType());
            auto ret = std::make_unique<RefType>(spanOf(ctx));
            ret->referent = std::move(innernode);
            ret->isMut = false;
            return ret;
        }
        throw std::logic_error("Invalid Closed Cast Type: Disappearance of & or &&");
    }
    else if (ctx->pathIdentSegment()) {
        auto ret = std::make_unique<PathType>(spanOf(ctx));
        for(auto *seg : ctx->typePathSegment()) {
            ret->segments.push_back(buildTypePathSegment(seg));
        }
        PathSegment ident = buildPathIdentSegment(ctx->pathIdentSegment());
        ident.typeArgs = buildGenericArgs(ctx->genericArgs());
        ret->segments.push_back(std::move(ident));
        return ret;
    }
    throw std::logic_error("Invalid Closed Cast Type: ??? ");
}

NodePtr<Expr> AstBuilder::buildClosedCastExpr(RxParser::ClosedCastExpressionContext *ctx) {
    if(ctx->unaryExpression()) {
        return buildUnaryExpr(ctx->unaryExpression());
    }
    auto ret = std::make_unique<CastExpr>(spanOf(ctx));
    ret->type = buildClosedCastType(ctx->closedCastType());
    ret->expr = buildCastExpr(ctx->castExpression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildUnaryExpr(RxParser::UnaryExpressionContext *ctx) {
    if(ctx->postfixExpression()) {
        return buildPostfixExpr(ctx->postfixExpression());
    }
    auto ret = std::make_unique<UnaryExpr>(spanOf(ctx));
    if(ctx->unaryOperator()->ANDAND()) {
        auto innernode = std::make_unique<UnaryExpr>(spanOf(ctx));
        innernode->num = buildUnaryExpr(ctx->unaryExpression());
        if(ctx->unaryOperator()->MUT()) {
            innernode->op = UnaryOp::RefMut;
        }
        else {
            innernode->op = UnaryOp::Ref;
        }
        ret->num = std::move(innernode);
        ret->op = UnaryOp::Ref;
    }
    else {
        ret->op = toUnaryOp(ctx->unaryOperator());
        ret->num = buildUnaryExpr(ctx->unaryExpression());
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildPostfixExpr(RxParser::PostfixExpressionContext *ctx) {

}

AstBuilder::AstBuilder(std::string prog) {

}

NodePtr<Crate> AstBuilder::build(RxParser::CrateContext *ctx) {

}

NodePtr<Crate> AstBuilder::parse(const std::string &src) {

}

std::string AstBuilder::text(antlr4::tree::ParseTree *tree) {

}

std::string AstBuilder::text(antlr4::Token *tok) {

}

std::string AstBuilder::ident(RxParser::IdentifierContext *ctx) {

}

IntValue AstBuilder::parseInteger(antlr4::Token *tok) {

}

NodePtr<Item> AstBuilder::buildItem(RxParser::ItemContext *ctx) {

}

NodePtr<Item> AstBuilder::buildUseDeclaration(RxParser::UseDeclarationContext *ctx) {
    return std::make_unique<UseDecItem>(spanOf(ctx), ctx->getText());
}

NodePtr<Item> AstBuilder::buildFunctionDefinition(RxParser::FunctionDefinitionContext *ctx) {

}

std::vector<NodePtr<FuncParam>> AstBuilder::buildFunctionParameters(RxParser::FunctionParametersContext *ctx) {

}

NodePtr<Item> AstBuilder::buildStructDefinition(RxParser::StructDefinitionContext *ctx) {

}

NodePtr<StructField> AstBuilder::buildStructField(RxParser::StructFieldContext *ctx) {

}

NodePtr<Item> AstBuilder::buildConstantItem(RxParser::ConstantItemContext *ctx) {

}

NodePtr<Item> AstBuilder::buildInherentImpl(RxParser::InherentImplContext *ctx) {

}

NodePtr<Item> AstBuilder::buildAssociatedItem(RxParser::AssociatedItemContext *ctx) {

}

NodePtr<Type> AstBuilder::buildReferenceType(RxParser::ReferenceTypeContext *ctx) {
    auto ret = std::make_unique<RefType>(spanOf(ctx));
    if(ctx->ANDAND()) {
        auto innernode = std::make_unique<RefType>(spanOf(ctx));
        innernode->isMut = ctx->MUT() ? true : false;
        innernode->referent = buildTypeRef(ctx->typeRef());
        ret->referent = std::move(innernode);
        ret->isMut = false;
    }
    else {
        ret->isMut = ctx->MUT() ? true : false;
        ret->referent = buildTypeRef(ctx->typeRef());
    }
    return ret;
}

NodePtr<Type> AstBuilder::buildArrayType(RxParser::ArrayTypeContext *ctx) {
    auto ret = std::make_unique<ArrayType>(spanOf(ctx));
    ret->elem = buildTypeRef(ctx->typeRef());
    ret->len = buildConstValue(ctx->constValue());
    return ret;
}

NodePtr<Type> AstBuilder::buildTypePath(RxParser::TypePathContext *ctx) {
    auto ret = std::make_unique<PathType>(spanOf(ctx));
    for(auto *segment: ctx->typePathSegment()) {
        ret->segments.push_back(buildTypePathSegment(segment));
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildPathInExpression(RxParser::PathInExpressionContext *ctx) {

}

PathSegment AstBuilder::buildPathSegment(RxParser::PathExprSegmentContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConstValue(RxParser::ConstValueContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildMagnitude(RxParser::MagnitudeContext *ctx) {

}

NodePtr<FuncParam> AstBuilder::buildSelfParam(RxParser::SelfParamContext *ctx) {

}

NodePtr<FuncParam> AstBuilder::buildFunctionParam(RxParser::FunctionParamContext *ctx) {

}

std::pair<std::string, bool> AstBuilder::buildIdentifierBinding(RxParser::IdentifierBindingContext *ctx) {

}

std::vector<Derive> AstBuilder::buildDerives(std::vector<RxParser::OuterAttributeContext *> ctxs) {

}

NodePtr<Expr> AstBuilder::applyPostfixSuffix(NodePtr<Expr> base, RxParser::PostfixSuffixContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildDotSuffix(NodePtr<Expr> base, RxParser::DotSuffixContext *ctx) {

}

std::vector<NodePtr<Expr>> AstBuilder::buildCallArguments(RxParser::CallArgumentsContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionExpr(RxParser::ConditionExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionAssignmentExpr(RxParser::ConditionAssignmentExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionLogicalOrExpr(RxParser::ConditionLogicalOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionLogicalAndExpr(RxParser::ConditionLogicalAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionComparisonExpr(RxParser::ConditionComparisonExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBitOrExpr(RxParser::ConditionBitOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedBitOrExpr(RxParser::ConditionClosedBitOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBitXorExpr(RxParser::ConditionBitXorExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedBitXorExpr(RxParser::ConditionClosedBitXorExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBitAndExpr(RxParser::ConditionBitAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedBitAndExpr(RxParser::ConditionClosedBitAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionShiftExpr(RxParser::ConditionShiftExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedShiftExpr(RxParser::ConditionClosedShiftExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionAdditiveExpr(RxParser::ConditionAdditiveExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedAdditiveExpr(RxParser::ConditionClosedAdditiveExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionMultiplicativeExpr(RxParser::ConditionMultiplicativeExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedMultiplicativeExpr(RxParser::ConditionClosedMultiplicativeExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionCastExpr(RxParser::ConditionCastExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionClosedCastExpr(RxParser::ConditionClosedCastExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionUnaryExpr(RxParser::ConditionUnaryExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionPostfixExpr(RxParser::ConditionPostfixExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakExpr(RxParser::ConditionBreakExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakAssignmentExpr(RxParser::ConditionBreakAssignmentExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakLogicalOrExpr(RxParser::ConditionBreakLogicalOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakLogicalAndExpr(RxParser::ConditionBreakLogicalAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakComparisonExpr(RxParser::ConditionBreakComparisonExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakBitOrExpr(RxParser::ConditionBreakBitOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedBitOrExpr(RxParser::ConditionBreakClosedBitOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakBitXorExpr(RxParser::ConditionBreakBitXorExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedBitXorExpr(RxParser::ConditionBreakClosedBitXorExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakBitAndExpr(RxParser::ConditionBreakBitAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedBitAndExpr(RxParser::ConditionBreakClosedBitAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakShiftExpr(RxParser::ConditionBreakShiftExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedShiftExpr(RxParser::ConditionBreakClosedShiftExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakAdditiveExpr(RxParser::ConditionBreakAdditiveExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedAdditiveExpr(RxParser::ConditionBreakClosedAdditiveExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakMultiplicativeExpr(RxParser::ConditionBreakMultiplicativeExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedMultiplicativeExpr(RxParser::ConditionBreakClosedMultiplicativeExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakCastExpr(RxParser::ConditionBreakCastExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedCastExpr(RxParser::ConditionBreakClosedCastExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakUnaryExpr(RxParser::ConditionBreakUnaryExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionBreakPostfixExpr(RxParser::ConditionBreakPostfixExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementExpr(RxParser::StatementExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementAssignmentExpr(RxParser::StatementAssignmentExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementLogicalOrExpr(RxParser::StatementLogicalOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementLogicalAndExpr(RxParser::StatementLogicalAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementComparisonExpr(RxParser::StatementComparisonExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementBitOrExpr(RxParser::StatementBitOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedBitOrExpr(RxParser::StatementClosedBitOrExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementBitXorExpr(RxParser::StatementBitXorExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedBitXorExpr(RxParser::StatementClosedBitXorExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementBitAndExpr(RxParser::StatementBitAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedBitAndExpr(RxParser::StatementClosedBitAndExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementShiftExpr(RxParser::StatementShiftExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedShiftExpr(RxParser::StatementClosedShiftExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementAdditiveExpr(RxParser::StatementAdditiveExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedAdditiveExpr(RxParser::StatementClosedAdditiveExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementMultiplicativeExpr(RxParser::StatementMultiplicativeExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedMultiplicativeExpr(RxParser::StatementClosedMultiplicativeExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementCastExpr(RxParser::StatementCastExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementClosedCastExpr(RxParser::StatementClosedCastExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementUnaryExpr(RxParser::StatementUnaryExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildStatementPostfixExpr(RxParser::StatementPostfixExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildPrimaryExpr(RxParser::PrimaryExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionPrimary(RxParser::ConditionPrimaryContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildConditionPrimaryWithoutBareBlock(RxParser::ConditionPrimaryWithoutBareBlockContext *ctx) {

}

NodePtr<Stmt> AstBuilder::buildStatement(RxParser::StatementContext *ctx) {

}

NodePtr<Stmt> AstBuilder::buildLetStatement(RxParser::LetStatementContext *ctx) {

}

NodePtr<BlockExpr> AstBuilder::buildBlockExpr(RxParser::BlockExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildExpressionWithBlock(RxParser::ExpressionWithBlockContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildIfExpr(RxParser::IfExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildLiteralExpr(RxParser::LiteralExpressionContext *ctx) {

}

NodePtr<Expr> AstBuilder::buildArrayExpr(RxParser::ArrayExpressionContext *ctx) {

}

std::vector<NodePtr<StructExprField>> AstBuilder::buildStructExprFields(RxParser::StructExprFieldsContext *ctx) {

}

NodePtr<StructExprField> AstBuilder::buildStructExprField(RxParser::StructExprFieldContext *ctx) {

}
