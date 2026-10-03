#include "AstBuilder.h"
#include "AST/AstNode.h"
#include "AST/expr.h"
#include "AST/item.h"
#include "AST/stmt.h"
#include "antlr4-runtime.h"
#include "../grammar/RxLexer.h"
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
    auto ret = buildPrimaryExpr(ctx->primaryExpression());
    for(auto *suffix: ctx->postfixSuffix()) {
        SourceSpan span = spanOf(suffix);
        span.beginCol = ret->span.beginCol;
        span.beginLine = ret->span.beginLine;
        if(suffix->callArguments()) {//CallExpr
            auto call = std::make_unique<CallExpr>(span);
            call->callee = std::move(ret);
            for(auto *arg: suffix->callArguments()->expression()) {
                call->args.push_back(buildExpr(arg));
            }
            ret = std::move(call);
        }
        else if(suffix->LBRACKET() && suffix->RBRACKET()) {//IndexExpr
            auto ind = std::make_unique<IndexExpr>(span);
            ind->index = buildExpr(suffix->expression());
            ind->base = std::move(ret);
            ret = std::move(ind);
        }
        else {//dotSuffix
            if(suffix->dotSuffix()->identifier()) {//FieldExpr
                auto field = std::make_unique<FieldExpr>(span);
                field->base = std::move(ret);
                field->field = ident(suffix->dotSuffix()->identifier());
                ret = std::move(field);
            }
            else {//MethodCallExpr
                auto met = std::make_unique<MethodCallExpr>(span);
                met->receiver = std::move(ret);
                PathSegment seg = buildPathExprSegment(suffix->dotSuffix()->pathExprSegment());
                met->method = seg.ident;
                met->methodTypeArgs = std::move(seg.typeArgs);
                for(auto *arg: suffix->dotSuffix()->callArguments()->expression()) {
                    met->args.push_back(buildExpr(arg));
                }
                ret = std::move(met);
            }
            
        }
    }
    return ret;
}

AstBuilder::AstBuilder(std::string prog) {
    program = prog;
}

NodePtr<Crate> AstBuilder::build(RxParser::CrateContext *ctx) {
    auto ret = std::make_unique<Crate>(spanOf(ctx));
    for(auto *item: ctx->item()) {
        ret->items.push_back(buildItem(item));
    }
    return ret;
}

NodePtr<Crate> AstBuilder::parse(const std::string &src) {
    antlr4::ANTLRInputStream input(src);
    RxLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    RxParser parser(&tokens);

    RxParser::CrateContext *tree = parser.crate();
    if(tree == nullptr || parser.getNumberOfSyntaxErrors() > 0) {
        return nullptr;
    }

    AstBuilder builder(src);
    return builder.build(tree);
}

NodePtr<AstNode> AstBuilder::parseEntry(const std::string &src, const std::string &entry) {
    antlr4::ANTLRInputStream input(src);
    RxLexer lexer(&input);
    antlr4::CommonTokenStream tokens(&lexer);
    RxParser parser(&tokens);
    parser.removeErrorListeners();

    antlr4::ParserRuleContext *tree = nullptr;
    if(entry == "crate")             tree = parser.crate();
    else if(entry == "item")         tree = parser.item();
    else if(entry == "expression")   tree = parser.expression();
    else if(entry == "typeRef")      tree = parser.typeRef();
    else if(entry == "letStatement") tree = parser.letStatement();
    else                             return nullptr;

    if(tree == nullptr || parser.getNumberOfSyntaxErrors() > 0) {
        return nullptr;
    }
    if(tokens.LA(1) != antlr4::Token::EOF) {
        return nullptr;
    }

    AstBuilder builder(src);
    if(entry == "crate") {
        return builder.build(static_cast<RxParser::CrateContext *>(tree));
    }
    if(entry == "item") {
        return builder.buildItem(static_cast<RxParser::ItemContext *>(tree));
    }
    if(entry == "expression") {
        return builder.buildExpr(static_cast<RxParser::ExpressionContext *>(tree));
    }
    if(entry == "typeRef") {
        return builder.buildTypeRef(static_cast<RxParser::TypeRefContext *>(tree));
    }
    if(entry == "letStatement") {
        return builder.buildLetStatement(static_cast<RxParser::LetStatementContext *>(tree));
    }
    return nullptr;
}

std::string AstBuilder::text(antlr4::tree::ParseTree *tree) {
    return tree->getText();
}

std::string AstBuilder::text(antlr4::Token *tok) {
    return tok->getText();
}

std::string AstBuilder::ident(RxParser::IdentifierContext *ctx) {
    return ctx->getText();
}

IntValue AstBuilder::parseInteger(antlr4::Token *tok) {
    IntValue ret;
    std::string lit = tok->getText();
    ret.literal = lit;
    if(lit.size() >= 3 && lit.substr(lit.size() - 3, 3) == "i32") {
        ret.suffix = IntSuffix::I32;
        lit = lit.substr(0, lit.size() - 3);
    }
    else if(lit.size() >= 3 && lit.substr(lit.size() - 3, 3) == "u32") {
        ret.suffix = IntSuffix::U32;
        lit = lit.substr(0, lit.size() - 3);
    }
    else if(lit.size() >= 5 && lit.substr(lit.size() - 5, 5) == "isize") {
        ret.suffix = IntSuffix::Isize;
        lit = lit.substr(0, lit.size() - 5);
    }
    else if(lit.size() >= 5 && lit.substr(lit.size() - 5, 5) == "usize") {
        ret.suffix = IntSuffix::Usize;
        lit = lit.substr(0, lit.size() - 5);
    }
    else {
        ret.suffix = IntSuffix::Infer;
    }

    size_t base = 10, fir = 0;
    if(lit.size() >= 2 && lit[0] == '0') {
        if(lit[1] == 'b') {
            base = 2;
            fir = 2;
        }
        else if(lit[1] == 'o') {
            base = 8;
            fir = 2;
        }
        else if(lit[1] == 'x') {
            base = 16;
            fir = 2;
        }
    }
    ret.val = 0;
    for(size_t i = fir ; i < lit.size(); i++) {
        if(lit[i] == '_') continue;
        char c = lit[i];
        unsigned d;
        if(c >= '0' && c <= '9') d = c - '0';
        else if(c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if(c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else continue;
        ret.val = ret.val * base + d;
    }
    return ret;
}

NodePtr<Item> AstBuilder::buildItem(RxParser::ItemContext *ctx) {
    if(ctx->useDeclaration()) {
        return buildUseDeclaration(ctx->useDeclaration());
    }
    else if(ctx->functionDefinition()) {
        return buildFunctionDefinition(ctx->functionDefinition());
    }
    else if(ctx->structDefinition()) {
        return buildStructDefinition(ctx->structDefinition());
    }
    else if(ctx->constantItem()) {
        return buildConstantItem(ctx->constantItem());
    }
    else if(ctx->inherentImpl()) {
        return buildInherentImpl(ctx->inherentImpl());
    }
    throw std::logic_error("Invalid Item");
}

NodePtr<Item> AstBuilder::buildUseDeclaration(RxParser::UseDeclarationContext *ctx) {
    return std::make_unique<UseDecItem>(spanOf(ctx), ctx->getText());
}

NodePtr<Item> AstBuilder::buildFunctionDefinition(RxParser::FunctionDefinitionContext *ctx) {
    auto ret = std::make_unique<FuncDefItem>(spanOf(ctx));
    ret->body = buildBlockExpr(ctx->blockExpression());
    ret->name = ident(ctx->identifier());
    ret->params = ctx->functionParameters() ? \
                  buildFunctionParameters(ctx->functionParameters()) : std::vector<NodePtr<FuncParam>>();
    ret->returnType = ctx->typeRef() ? buildTypeRef(ctx->typeRef()) : nullptr;
    return ret;
}

std::vector<NodePtr<FuncParam>> AstBuilder::buildFunctionParameters(RxParser::FunctionParametersContext *ctx) {
    std::vector<NodePtr<FuncParam>> params;
    if(ctx->selfParam()) {
        params.push_back(buildSelfParam(ctx->selfParam()));
    }
    for(auto *param: ctx->functionParam()) {
        params.push_back(buildFunctionParam(param));
    }
    return params;
}

NodePtr<Item> AstBuilder::buildStructDefinition(RxParser::StructDefinitionContext *ctx) {
    auto ret = std::make_unique<StructDefItem>(spanOf(ctx));
    ret->derives = buildDerives(ctx->outerAttribute());
    ret->name = ident(ctx->identifier());
    for(auto field: ctx->structField()) {
        ret->fields.push_back(buildStructField(field));
    }
    return ret;
}

NodePtr<StructField> AstBuilder::buildStructField(RxParser::StructFieldContext *ctx) {
    auto ret = std::make_unique<StructField>(spanOf(ctx));
    ret->name = ident(ctx->identifier());
    ret->type = buildTypeRef(ctx->typeRef());
    return ret;
}

NodePtr<Item> AstBuilder::buildConstantItem(RxParser::ConstantItemContext *ctx) {
    auto ret = std::make_unique<ConstItem>(spanOf(ctx));
    ret->name = ident(ctx->identifier());
    ret->type = buildTypeRef(ctx->typeRef());
    ret->value = buildConstValue(ctx->constValue());
    return ret;
}

NodePtr<Item> AstBuilder::buildInherentImpl(RxParser::InherentImplContext *ctx) {
    auto ret = std::make_unique<ImplItem>(spanOf(ctx));
    ret->selfType = buildTypeRef(ctx->typeRef());
    for(auto *item: ctx->associatedItem()) {
        ret->items.push_back(buildAssociatedItem(item));
    }
    return ret;
}

NodePtr<Item> AstBuilder::buildAssociatedItem(RxParser::AssociatedItemContext *ctx) {
    if(ctx->constantItem()) {
        return buildConstantItem(ctx->constantItem());
    }
    else {
        return buildFunctionDefinition(ctx->functionDefinition());
    }
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

NodePtr<PathExpr> AstBuilder::buildPathInExpression(RxParser::PathInExpressionContext *ctx) {
    auto ret = std::make_unique<PathExpr>(spanOf(ctx));
    for(auto *seg: ctx->pathExprSegment()) {
        ret->segments.push_back(buildPathExprSegment(seg));
    }
    return ret;
}

PathSegment AstBuilder::buildPathExprSegment(RxParser::PathExprSegmentContext *ctx) {
    PathSegment seg = buildPathIdentSegment(ctx->pathIdentSegment());
    if(ctx->genericArgs()) {
        seg.typeArgs = buildGenericArgs(ctx->genericArgs());
    }
    return seg;
}

NodePtr<Expr> AstBuilder::buildConstValue(RxParser::ConstValueContext *ctx) {
    if(ctx->INTEGER_LITERAL()) {
        return std::make_unique<IntExpr>(spanOf(ctx),
                                         parseInteger(ctx->INTEGER_LITERAL()->getSymbol()));
    }
    else if(ctx->TRUE()) {
        return std::make_unique<BoolExpr>(spanOf(ctx), true);
    }
    else if(ctx->FALSE()) {
        return std::make_unique<BoolExpr>(spanOf(ctx), false);
    }
    else if(ctx->pathInExpression()) {
        return buildPathInExpression(ctx->pathInExpression());
    }
    else if(ctx->MINUS()) {
        auto ret = std::make_unique<UnaryExpr>(spanOf(ctx));
        ret->op = UnaryOp::Neg;
        ret->num = buildMagnitude(ctx->magnitude());
        return ret;
    }
    else if(ctx->LPAREN() && ctx->RPAREN()) {
        auto ret = std::make_unique<GroupExpr>(spanOf(ctx));
        ret->inner = buildConstValue(ctx->constValue());
        return ret;
    }
    throw std::logic_error("Invalid ConstValue");
}

NodePtr<Expr> AstBuilder::buildMagnitude(RxParser::MagnitudeContext *ctx) {
    if(ctx->INTEGER_LITERAL()) {
        return std::make_unique<IntExpr>(spanOf(ctx),
                                         parseInteger(ctx->INTEGER_LITERAL()->getSymbol()));
    }
    else if(ctx->pathInExpression()) {
        return buildPathInExpression(ctx->pathInExpression());
    }
    else if(ctx->LPAREN() && ctx->RPAREN()) {
        auto ret = std::make_unique<GroupExpr>(spanOf(ctx));
        ret->inner = buildMagnitude(ctx->magnitude());
        return ret;
    }
    throw std::logic_error("Invalid Magnitude");
}

NodePtr<FuncParam> AstBuilder::buildSelfParam(RxParser::SelfParamContext *ctx) {
    auto ret = std::make_unique<FuncParam>(spanOf(ctx));
    ret->isSelf = true;
    ret->byRef = ctx->AMP() ? true : false;
    ret->isMut = ctx->MUT() ? true : false;
    ret->name = "self";
    return ret;
}

NodePtr<FuncParam> AstBuilder::buildFunctionParam(RxParser::FunctionParamContext *ctx) {
    auto ret = std::make_unique<FuncParam>(spanOf(ctx));
    auto id = buildIdentifierBinding(ctx->identifierBinding());
    ret->name = id.first;
    ret->isSelf = false;
    ret->isMut = id.second;
    ret->type = buildTypeRef(ctx->typeRef());
    ret->byRef = false;
    return ret;
}

std::pair<std::string, bool> AstBuilder::buildIdentifierBinding(RxParser::IdentifierBindingContext *ctx) {
    std::pair<std::string, bool> ret;
    ret.first = ident(ctx->identifier());
    ret.second = ctx->MUT() ? true : false;
    return ret;
}

std::vector<Derive> AstBuilder::buildDerives(std::vector<RxParser::OuterAttributeContext *> ctxs) {
    std::vector<Derive> derives;
    for(auto *attr : ctxs) {
        for(auto *name : attr->deriveName()) {
            derives.push_back(toDerive(name));
        }
    }
    return derives;
}

NodePtr<Expr> AstBuilder::buildConditionExpr(RxParser::ConditionExpressionContext *ctx) {
    return buildConditionAssignmentExpr(ctx->conditionAssignmentExpression());
}

NodePtr<Expr> AstBuilder::buildConditionAssignmentExpr(RxParser::ConditionAssignmentExpressionContext *ctx) {
    if(!ctx->assignmentOperator()) {
        return buildConditionLogicalOrExpr(ctx->conditionLogicalOrExpression());
    }
    auto ret = std::make_unique<AssignExpr>(spanOf(ctx));
    ret->lhs = buildConditionLogicalOrExpr(ctx->conditionLogicalOrExpression());
    ret->op = toAssignOp(ctx->assignmentOperator());
    ret->rhs = buildConditionExpr(ctx->conditionExpression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionLogicalOrExpr(RxParser::ConditionLogicalOrExpressionContext *ctx) {
    const auto operands = ctx->conditionLogicalAndExpression();
    auto lhs = buildConditionLogicalAndExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::LogicalOr, std::move(lhs), buildConditionLogicalAndExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionLogicalAndExpr(RxParser::ConditionLogicalAndExpressionContext *ctx) {
    const auto operands = ctx->conditionComparisonExpression();
    auto lhs = buildConditionComparisonExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::LogicalAnd, std::move(lhs), buildConditionComparisonExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionComparisonExpr(RxParser::ConditionComparisonExpressionContext *ctx) {
    if(!ctx->comparisonExceptLt() && !ctx->LT()) {
        return buildConditionBitOrExpr(ctx->conditionBitOrExpression()[0]);
    }
    if(ctx->LT()) {
        return binary(BinaryOp::Lt,
                      buildConditionClosedBitOrExpr(ctx->conditionClosedBitOrExpression()),
                      buildConditionBitOrExpr(ctx->conditionBitOrExpression()[0]));
    }
    return binary(toBinaryOp(ctx->comparisonExceptLt()),
                  buildConditionBitOrExpr(ctx->conditionBitOrExpression()[0]),
                  buildConditionBitOrExpr(ctx->conditionBitOrExpression()[1]));
}

NodePtr<Expr> AstBuilder::buildConditionBitOrExpr(RxParser::ConditionBitOrExpressionContext *ctx) {
    const auto operands = ctx->conditionBitXorExpression();
    auto lhs = buildConditionBitXorExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), buildConditionBitXorExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionClosedBitOrExpr(RxParser::ConditionClosedBitOrExpressionContext *ctx) {
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->conditionBitXorExpression()) {
        operands.push_back(buildConditionBitXorExpr(operand));
    }
    operands.push_back(buildConditionClosedBitXorExpr(ctx->conditionClosedBitXorExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBitXorExpr(RxParser::ConditionBitXorExpressionContext *ctx) {
    const auto operands = ctx->conditionBitAndExpression();
    auto lhs = buildConditionBitAndExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), buildConditionBitAndExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionClosedBitXorExpr(RxParser::ConditionClosedBitXorExpressionContext *ctx) {
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->conditionBitAndExpression()) {
        operands.push_back(buildConditionBitAndExpr(operand));
    }
    operands.push_back(buildConditionClosedBitAndExpr(ctx->conditionClosedBitAndExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBitAndExpr(RxParser::ConditionBitAndExpressionContext *ctx) {
    const auto operands = ctx->conditionShiftExpression();
    auto lhs = buildConditionShiftExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), buildConditionShiftExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionClosedBitAndExpr(RxParser::ConditionClosedBitAndExpressionContext *ctx) {
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->conditionShiftExpression()) {
        operands.push_back(buildConditionShiftExpr(operand));
    }
    operands.push_back(buildConditionClosedShiftExpr(ctx->conditionClosedShiftExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionShiftExpr(RxParser::ConditionShiftExpressionContext *ctx) {
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *closed = dynamic_cast<RxParser::ConditionClosedAdditiveExpressionContext *>(child)) {
            return buildConditionClosedAdditiveExpr(closed);
        }
        return buildConditionAdditiveExpr(dynamic_cast<RxParser::ConditionAdditiveExpressionContext *>(child));
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

NodePtr<Expr> AstBuilder::buildConditionClosedShiftExpr(RxParser::ConditionClosedShiftExpressionContext *ctx) {
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *closed = dynamic_cast<RxParser::ConditionClosedAdditiveExpressionContext *>(child)) {
            return buildConditionClosedAdditiveExpr(closed);
        }
        return buildConditionAdditiveExpr(dynamic_cast<RxParser::ConditionAdditiveExpressionContext *>(child));
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

NodePtr<Expr> AstBuilder::buildConditionAdditiveExpr(RxParser::ConditionAdditiveExpressionContext *ctx) {
    const auto operands = ctx->conditionMultiplicativeExpression();
    const auto ops = ctx->additiveOperator();
    auto lhs = buildConditionMultiplicativeExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), buildConditionMultiplicativeExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionClosedAdditiveExpr(RxParser::ConditionClosedAdditiveExpressionContext *ctx) {
    const auto ops = ctx->additiveOperator();
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->conditionMultiplicativeExpression()) {
        operands.push_back(buildConditionMultiplicativeExpr(operand));
    }
    operands.push_back(buildConditionClosedMultiplicativeExpr(ctx->conditionClosedMultiplicativeExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionMultiplicativeExpr(RxParser::ConditionMultiplicativeExpressionContext *ctx) {
    const auto operands = ctx->conditionCastExpression();
    const auto ops = ctx->multiplicativeOperator();
    auto lhs = buildConditionCastExpr(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), buildConditionCastExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionClosedMultiplicativeExpr(RxParser::ConditionClosedMultiplicativeExpressionContext *ctx) {
    const auto ops = ctx->multiplicativeOperator();
    std::vector<NodePtr<Expr>> operands;
    for(auto *operand : ctx->conditionCastExpression()) {
        operands.push_back(buildConditionCastExpr(operand));
    }
    operands.push_back(buildConditionClosedCastExpr(ctx->conditionClosedCastExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionCastExpr(RxParser::ConditionCastExpressionContext *ctx) {
    auto lhs = buildConditionUnaryExpr(ctx->conditionUnaryExpression());
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

NodePtr<Expr> AstBuilder::buildConditionClosedCastExpr(RxParser::ConditionClosedCastExpressionContext *ctx) {
    if(ctx->conditionUnaryExpression()) {
        return buildConditionUnaryExpr(ctx->conditionUnaryExpression());
    }
    auto ret = std::make_unique<CastExpr>(spanOf(ctx));
    ret->type = buildClosedCastType(ctx->closedCastType());
    ret->expr = buildConditionCastExpr(ctx->conditionCastExpression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionUnaryExpr(RxParser::ConditionUnaryExpressionContext *ctx) {
    if(ctx->conditionPostfixExpression()) {
        return buildConditionPostfixExpr(ctx->conditionPostfixExpression());
    }
    auto ret = std::make_unique<UnaryExpr>(spanOf(ctx));
    if(ctx->unaryOperator()->ANDAND()) {
        auto innernode = std::make_unique<UnaryExpr>(spanOf(ctx));
        innernode->num = buildConditionUnaryExpr(ctx->conditionUnaryExpression());
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
        ret->num = buildConditionUnaryExpr(ctx->conditionUnaryExpression());
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionPostfixExpr(RxParser::ConditionPostfixExpressionContext *ctx) {
    auto ret = buildConditionPrimary(ctx->conditionPrimary());
    for(auto *suffix: ctx->postfixSuffix()) {
        SourceSpan span = spanOf(suffix);
        span.beginCol = ret->span.beginCol;
        span.beginLine = ret->span.beginLine;
        if(suffix->callArguments()) {//CallExpr
            auto call = std::make_unique<CallExpr>(span);
            call->callee = std::move(ret);
            for(auto *arg: suffix->callArguments()->expression()) {
                call->args.push_back(buildExpr(arg));
            }
            ret = std::move(call);
        }
        else if(suffix->LBRACKET() && suffix->RBRACKET()) {//IndexExpr
            auto ind = std::make_unique<IndexExpr>(span);
            ind->index = buildExpr(suffix->expression());
            ind->base = std::move(ret);
            ret = std::move(ind);
        }
        else {//dotSuffix
            if(suffix->dotSuffix()->identifier()) {//FieldExpr
                auto field = std::make_unique<FieldExpr>(span);
                field->base = std::move(ret);
                field->field = ident(suffix->dotSuffix()->identifier());
                ret = std::move(field);
            }
            else {//MethodCallExpr
                auto met = std::make_unique<MethodCallExpr>(span);
                met->receiver = std::move(ret);
                PathSegment seg = buildPathExprSegment(suffix->dotSuffix()->pathExprSegment());
                met->method = seg.ident;
                met->methodTypeArgs = std::move(seg.typeArgs);
                for(auto *arg: suffix->dotSuffix()->callArguments()->expression()) {
                    met->args.push_back(buildExpr(arg));
                }
                ret = std::move(met);
            }
        }
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionBreakExpr(RxParser::ConditionBreakExpressionContext *ctx) {
    return buildConditionBreakAssignmentExpr(ctx->conditionBreakAssignmentExpression());
}

NodePtr<Expr> AstBuilder::buildConditionBreakAssignmentExpr(RxParser::ConditionBreakAssignmentExpressionContext *ctx) {
    if(!ctx->assignmentOperator()) {
        return buildConditionBreakLogicalOrExpr(ctx->conditionBreakLogicalOrExpression());
    }
    auto ret = std::make_unique<AssignExpr>(spanOf(ctx));
    ret->lhs = buildConditionBreakLogicalOrExpr(ctx->conditionBreakLogicalOrExpression());
    ret->op = toAssignOp(ctx->assignmentOperator());
    ret->rhs = buildConditionExpr(ctx->conditionExpression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionBreakLogicalOrExpr(RxParser::ConditionBreakLogicalOrExpressionContext *ctx) {
    auto lhs = buildConditionBreakLogicalAndExpr(ctx->conditionBreakLogicalAndExpression());
    for(auto *operand : ctx->conditionLogicalAndExpression()) {
        lhs = binary(BinaryOp::LogicalOr, std::move(lhs), buildConditionLogicalAndExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakLogicalAndExpr(RxParser::ConditionBreakLogicalAndExpressionContext *ctx) {
    auto lhs = buildConditionBreakComparisonExpr(ctx->conditionBreakComparisonExpression());
    for(auto *operand : ctx->conditionComparisonExpression()) {
        lhs = binary(BinaryOp::LogicalAnd, std::move(lhs), buildConditionComparisonExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakComparisonExpr(RxParser::ConditionBreakComparisonExpressionContext *ctx) {
    if(!ctx->comparisonExceptLt() && !ctx->LT()) {
        return buildConditionBreakBitOrExpr(ctx->conditionBreakBitOrExpression());
    }
    if(ctx->LT()) {
        return binary(BinaryOp::Lt,
                      buildConditionBreakClosedBitOrExpr(ctx->conditionBreakClosedBitOrExpression()),
                      buildConditionBitOrExpr(ctx->conditionBitOrExpression()));
    }
    return binary(toBinaryOp(ctx->comparisonExceptLt()),
                  buildConditionBreakBitOrExpr(ctx->conditionBreakBitOrExpression()),
                  buildConditionBitOrExpr(ctx->conditionBitOrExpression()));
}

NodePtr<Expr> AstBuilder::buildConditionBreakBitOrExpr(RxParser::ConditionBreakBitOrExpressionContext *ctx) {
    auto lhs = buildConditionBreakBitXorExpr(ctx->conditionBreakBitXorExpression());
    for(auto *operand : ctx->conditionBitXorExpression()) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), buildConditionBitXorExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedBitOrExpr(RxParser::ConditionBreakClosedBitOrExpressionContext *ctx) {
    if(ctx->conditionBreakClosedBitXorExpression()) {
        return buildConditionBreakClosedBitXorExpr(ctx->conditionBreakClosedBitXorExpression());
    }
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildConditionBreakBitXorExpr(ctx->conditionBreakBitXorExpression()));
    for(auto *operand : ctx->conditionBitXorExpression()) {
        operands.push_back(buildConditionBitXorExpr(operand));
    }
    operands.push_back(buildConditionClosedBitXorExpr(ctx->conditionClosedBitXorExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakBitXorExpr(RxParser::ConditionBreakBitXorExpressionContext *ctx) {
    auto lhs = buildConditionBreakBitAndExpr(ctx->conditionBreakBitAndExpression());
    for(auto *operand : ctx->conditionBitAndExpression()) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), buildConditionBitAndExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedBitXorExpr(RxParser::ConditionBreakClosedBitXorExpressionContext *ctx) {
    if(ctx->conditionBreakClosedBitAndExpression()) {
        return buildConditionBreakClosedBitAndExpr(ctx->conditionBreakClosedBitAndExpression());
    }
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildConditionBreakBitAndExpr(ctx->conditionBreakBitAndExpression()));
    for(auto *operand : ctx->conditionBitAndExpression()) {
        operands.push_back(buildConditionBitAndExpr(operand));
    }
    operands.push_back(buildConditionClosedBitAndExpr(ctx->conditionClosedBitAndExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakBitAndExpr(RxParser::ConditionBreakBitAndExpressionContext *ctx) {
    auto lhs = buildConditionBreakShiftExpr(ctx->conditionBreakShiftExpression());
    for(auto *operand : ctx->conditionShiftExpression()) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), buildConditionShiftExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedBitAndExpr(RxParser::ConditionBreakClosedBitAndExpressionContext *ctx) {
    if(ctx->conditionBreakClosedShiftExpression()) {
        return buildConditionBreakClosedShiftExpr(ctx->conditionBreakClosedShiftExpression());
    }
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildConditionBreakShiftExpr(ctx->conditionBreakShiftExpression()));
    for(auto *operand : ctx->conditionShiftExpression()) {
        operands.push_back(buildConditionShiftExpr(operand));
    }
    operands.push_back(buildConditionClosedShiftExpr(ctx->conditionClosedShiftExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakShiftExpr(RxParser::ConditionBreakShiftExpressionContext *ctx) {
    if(ctx->SHL().empty() && ctx->shiftRight().empty()) {
        return buildConditionBreakAdditiveExpr(ctx->conditionBreakAdditiveExpression());
    }
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *bc = dynamic_cast<RxParser::ConditionBreakClosedAdditiveExpressionContext *>(child)) {
            return buildConditionBreakClosedAdditiveExpr(bc);
        }
        if(auto *ba = dynamic_cast<RxParser::ConditionBreakAdditiveExpressionContext *>(child)) {
            return buildConditionBreakAdditiveExpr(ba);
        }
        if(auto *oc = dynamic_cast<RxParser::ConditionClosedAdditiveExpressionContext *>(child)) {
            return buildConditionClosedAdditiveExpr(oc);
        }
        return buildConditionAdditiveExpr(dynamic_cast<RxParser::ConditionAdditiveExpressionContext *>(child));
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

NodePtr<Expr> AstBuilder::buildConditionBreakClosedShiftExpr(RxParser::ConditionBreakClosedShiftExpressionContext *ctx) {
    if(ctx->SHL().empty() && ctx->shiftRight().empty()) {
        return buildConditionBreakClosedAdditiveExpr(ctx->conditionBreakClosedAdditiveExpression());
    }
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *bc = dynamic_cast<RxParser::ConditionBreakClosedAdditiveExpressionContext *>(child)) {
            return buildConditionBreakClosedAdditiveExpr(bc);
        }
        if(auto *ba = dynamic_cast<RxParser::ConditionBreakAdditiveExpressionContext *>(child)) {
            return buildConditionBreakAdditiveExpr(ba);
        }
        if(auto *oc = dynamic_cast<RxParser::ConditionClosedAdditiveExpressionContext *>(child)) {
            return buildConditionClosedAdditiveExpr(oc);
        }
        return buildConditionAdditiveExpr(dynamic_cast<RxParser::ConditionAdditiveExpressionContext *>(child));
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

NodePtr<Expr> AstBuilder::buildConditionBreakAdditiveExpr(RxParser::ConditionBreakAdditiveExpressionContext *ctx) {
    const auto ops = ctx->additiveOperator();
    auto lhs = buildConditionBreakMultiplicativeExpr(ctx->conditionBreakMultiplicativeExpression());
    const auto operands = ctx->conditionMultiplicativeExpression();
    for(size_t i = 0; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i]), std::move(lhs), buildConditionMultiplicativeExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedAdditiveExpr(RxParser::ConditionBreakClosedAdditiveExpressionContext *ctx) {
    if(ctx->conditionBreakClosedMultiplicativeExpression()) {
        return buildConditionBreakClosedMultiplicativeExpr(ctx->conditionBreakClosedMultiplicativeExpression());
    }
    const auto ops = ctx->additiveOperator();
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildConditionBreakMultiplicativeExpr(ctx->conditionBreakMultiplicativeExpression()));
    for(auto *operand : ctx->conditionMultiplicativeExpression()) {
        operands.push_back(buildConditionMultiplicativeExpr(operand));
    }
    operands.push_back(buildConditionClosedMultiplicativeExpr(ctx->conditionClosedMultiplicativeExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakMultiplicativeExpr(RxParser::ConditionBreakMultiplicativeExpressionContext *ctx) {
    const auto ops = ctx->multiplicativeOperator();
    auto lhs = buildConditionBreakCastExpr(ctx->conditionBreakCastExpression());
    const auto operands = ctx->conditionCastExpression();
    for(size_t i = 0; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i]), std::move(lhs), buildConditionCastExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakClosedMultiplicativeExpr(RxParser::ConditionBreakClosedMultiplicativeExpressionContext *ctx) {
    if(ctx->conditionBreakClosedCastExpression()) {
        return buildConditionBreakClosedCastExpr(ctx->conditionBreakClosedCastExpression());
    }
    const auto ops = ctx->multiplicativeOperator();
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildConditionBreakCastExpr(ctx->conditionBreakCastExpression()));
    for(auto *operand : ctx->conditionCastExpression()) {
        operands.push_back(buildConditionCastExpr(operand));
    }
    operands.push_back(buildConditionClosedCastExpr(ctx->conditionClosedCastExpression()));
    auto lhs = std::move(operands[0]);
    for(size_t i = 1; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i - 1]), std::move(lhs), std::move(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildConditionBreakCastExpr(RxParser::ConditionBreakCastExpressionContext *ctx) {
    auto lhs = buildConditionBreakUnaryExpr(ctx->conditionBreakUnaryExpression());
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

NodePtr<Expr> AstBuilder::buildConditionBreakClosedCastExpr(RxParser::ConditionBreakClosedCastExpressionContext *ctx) {
    if(ctx->conditionBreakUnaryExpression()) {
        return buildConditionBreakUnaryExpr(ctx->conditionBreakUnaryExpression());
    }
    auto ret = std::make_unique<CastExpr>(spanOf(ctx));
    ret->type = buildClosedCastType(ctx->closedCastType());
    ret->expr = buildConditionBreakCastExpr(ctx->conditionBreakCastExpression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionBreakUnaryExpr(RxParser::ConditionBreakUnaryExpressionContext *ctx) {
    if(ctx->conditionBreakPostfixExpression()) {
        return buildConditionBreakPostfixExpr(ctx->conditionBreakPostfixExpression());
    }
    auto ret = std::make_unique<UnaryExpr>(spanOf(ctx));
    if(ctx->unaryOperator()->ANDAND()) {
        auto innernode = std::make_unique<UnaryExpr>(spanOf(ctx));
        innernode->num = buildConditionUnaryExpr(ctx->conditionUnaryExpression());
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
        ret->num = buildConditionUnaryExpr(ctx->conditionUnaryExpression());
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildConditionBreakPostfixExpr(RxParser::ConditionBreakPostfixExpressionContext *ctx) {
    auto ret = buildConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
    for(auto *suffix: ctx->postfixSuffix()) {
        SourceSpan span = spanOf(suffix);
        span.beginCol = ret->span.beginCol;
        span.beginLine = ret->span.beginLine;
        if(suffix->callArguments()) {//CallExpr
            auto call = std::make_unique<CallExpr>(span);
            call->callee = std::move(ret);
            for(auto *arg: suffix->callArguments()->expression()) {
                call->args.push_back(buildExpr(arg));
            }
            ret = std::move(call);
        }
        else if(suffix->LBRACKET() && suffix->RBRACKET()) {//IndexExpr
            auto ind = std::make_unique<IndexExpr>(span);
            ind->index = buildExpr(suffix->expression());
            ind->base = std::move(ret);
            ret = std::move(ind);
        }
        else {//dotSuffix
            if(suffix->dotSuffix()->identifier()) {//FieldExpr
                auto field = std::make_unique<FieldExpr>(span);
                field->base = std::move(ret);
                field->field = ident(suffix->dotSuffix()->identifier());
                ret = std::move(field);
            }
            else {//MethodCallExpr
                auto met = std::make_unique<MethodCallExpr>(span);
                met->receiver = std::move(ret);
                PathSegment seg = buildPathExprSegment(suffix->dotSuffix()->pathExprSegment());
                met->method = seg.ident;
                met->methodTypeArgs = std::move(seg.typeArgs);
                for(auto *arg: suffix->dotSuffix()->callArguments()->expression()) {
                    met->args.push_back(buildExpr(arg));
                }
                ret = std::move(met);
            }
        }
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildStatementExpr(RxParser::StatementExpressionContext *ctx) {
    return buildStatementAssignmentExpr(ctx->statementAssignmentExpression());
}

NodePtr<Expr> AstBuilder::buildStatementAssignmentExpr(RxParser::StatementAssignmentExpressionContext *ctx) {
    if(!ctx->assignmentOperator()) {
        return buildStatementLogicalOrExpr(ctx->statementLogicalOrExpression());
    }
    auto ret = std::make_unique<AssignExpr>(spanOf(ctx));
    ret->lhs = buildStatementLogicalOrExpr(ctx->statementLogicalOrExpression());
    ret->op = toAssignOp(ctx->assignmentOperator());
    ret->rhs = buildExpr(ctx->expression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildStatementLogicalOrExpr(RxParser::StatementLogicalOrExpressionContext *ctx) {
    auto lhs = buildStatementLogicalAndExpr(ctx->statementLogicalAndExpression());
    for(auto *operand : ctx->logicalAndExpression()) {
        lhs = binary(BinaryOp::LogicalOr, std::move(lhs), buildLogicalAndExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementLogicalAndExpr(RxParser::StatementLogicalAndExpressionContext *ctx) {
    auto lhs = buildStatementComparisonExpr(ctx->statementComparisonExpression());
    for(auto *operand : ctx->comparisonExpression()) {
        lhs = binary(BinaryOp::LogicalAnd, std::move(lhs), buildComparisonExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementComparisonExpr(RxParser::StatementComparisonExpressionContext *ctx) {
    if(!ctx->comparisonExceptLt() && !ctx->LT()) {
        return buildStatementBitOrExpr(ctx->statementBitOrExpression());
    }
    if(ctx->LT()) {
        return binary(BinaryOp::Lt,
                      buildStatementClosedBitOrExpr(ctx->statementClosedBitOrExpression()),
                      buildBitOrExpr(ctx->bitOrExpression()));
    }
    return binary(toBinaryOp(ctx->comparisonExceptLt()),
                  buildStatementBitOrExpr(ctx->statementBitOrExpression()),
                  buildBitOrExpr(ctx->bitOrExpression()));
}

NodePtr<Expr> AstBuilder::buildStatementBitOrExpr(RxParser::StatementBitOrExpressionContext *ctx) {
    auto lhs = buildStatementBitXorExpr(ctx->statementBitXorExpression());
    for(auto *operand : ctx->bitXorExpression()) {
        lhs = binary(BinaryOp::BitOr, std::move(lhs), buildBitXorExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementClosedBitOrExpr(RxParser::StatementClosedBitOrExpressionContext *ctx) {
    if(ctx->statementClosedBitXorExpression()) {
        return buildStatementClosedBitXorExpr(ctx->statementClosedBitXorExpression());
    }
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildStatementBitXorExpr(ctx->statementBitXorExpression()));
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

NodePtr<Expr> AstBuilder::buildStatementBitXorExpr(RxParser::StatementBitXorExpressionContext *ctx) {
    auto lhs = buildStatementBitAndExpr(ctx->statementBitAndExpression());
    for(auto *operand : ctx->bitAndExpression()) {
        lhs = binary(BinaryOp::BitXor, std::move(lhs), buildBitAndExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementClosedBitXorExpr(RxParser::StatementClosedBitXorExpressionContext *ctx) {
    if(ctx->statementClosedBitAndExpression()) {
        return buildStatementClosedBitAndExpr(ctx->statementClosedBitAndExpression());
    }
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildStatementBitAndExpr(ctx->statementBitAndExpression()));
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

NodePtr<Expr> AstBuilder::buildStatementBitAndExpr(RxParser::StatementBitAndExpressionContext *ctx) {
    auto lhs = buildStatementShiftExpr(ctx->statementShiftExpression());
    for(auto *operand : ctx->shiftExpression()) {
        lhs = binary(BinaryOp::BitAnd, std::move(lhs), buildShiftExpr(operand));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementClosedBitAndExpr(RxParser::StatementClosedBitAndExpressionContext *ctx) {
    if(ctx->statementClosedShiftExpression()) {
        return buildStatementClosedShiftExpr(ctx->statementClosedShiftExpression());
    }
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildStatementShiftExpr(ctx->statementShiftExpression()));
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

NodePtr<Expr> AstBuilder::buildStatementShiftExpr(RxParser::StatementShiftExpressionContext *ctx) {
    if(ctx->SHL().empty() && ctx->shiftRight().empty()) {
        return buildStatementAdditiveExpr(ctx->statementAdditiveExpression());
    }
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *sc = dynamic_cast<RxParser::StatementClosedAdditiveExpressionContext *>(child)) {
            return buildStatementClosedAdditiveExpr(sc);
        }
        if(auto *sa = dynamic_cast<RxParser::StatementAdditiveExpressionContext *>(child)) {
            return buildStatementAdditiveExpr(sa);
        }
        if(auto *oc = dynamic_cast<RxParser::ClosedAdditiveExpressionContext *>(child)) {
            return buildClosedAdditiveExpr(oc);
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

NodePtr<Expr> AstBuilder::buildStatementClosedShiftExpr(RxParser::StatementClosedShiftExpressionContext *ctx) {
    if(ctx->SHL().empty() && ctx->shiftRight().empty()) {
        return buildStatementClosedAdditiveExpr(ctx->statementClosedAdditiveExpression());
    }
    auto operandAt = [&](antlr4::tree::ParseTree *child) -> NodePtr<Expr> {
        if(auto *sc = dynamic_cast<RxParser::StatementClosedAdditiveExpressionContext *>(child)) {
            return buildStatementClosedAdditiveExpr(sc);
        }
        if(auto *sa = dynamic_cast<RxParser::StatementAdditiveExpressionContext *>(child)) {
            return buildStatementAdditiveExpr(sa);
        }
        if(auto *oc = dynamic_cast<RxParser::ClosedAdditiveExpressionContext *>(child)) {
            return buildClosedAdditiveExpr(oc);
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

NodePtr<Expr> AstBuilder::buildStatementAdditiveExpr(RxParser::StatementAdditiveExpressionContext *ctx) {
    const auto ops = ctx->additiveOperator();
    auto lhs = buildStatementMultiplicativeExpr(ctx->statementMultiplicativeExpression());
    const auto operands = ctx->multiplicativeExpression();
    for(size_t i = 0; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i]), std::move(lhs), buildMultiplicativeExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementClosedAdditiveExpr(RxParser::StatementClosedAdditiveExpressionContext *ctx) {
    if(ctx->statementClosedMultiplicativeExpression()) {
        return buildStatementClosedMultiplicativeExpr(ctx->statementClosedMultiplicativeExpression());
    }
    const auto ops = ctx->additiveOperator();
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildStatementMultiplicativeExpr(ctx->statementMultiplicativeExpression()));
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

NodePtr<Expr> AstBuilder::buildStatementMultiplicativeExpr(RxParser::StatementMultiplicativeExpressionContext *ctx) {
    const auto ops = ctx->multiplicativeOperator();
    auto lhs = buildStatementCastExpr(ctx->statementCastExpression());
    const auto operands = ctx->castExpression();
    for(size_t i = 0; i < operands.size(); i++) {
        lhs = binary(toBinaryOp(ops[i]), std::move(lhs), buildCastExpr(operands[i]));
    }
    return lhs;
}

NodePtr<Expr> AstBuilder::buildStatementClosedMultiplicativeExpr(RxParser::StatementClosedMultiplicativeExpressionContext *ctx) {
    if(ctx->statementClosedCastExpression()) {
        return buildStatementClosedCastExpr(ctx->statementClosedCastExpression());
    }
    const auto ops = ctx->multiplicativeOperator();
    std::vector<NodePtr<Expr>> operands;
    operands.push_back(buildStatementCastExpr(ctx->statementCastExpression()));
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

NodePtr<Expr> AstBuilder::buildStatementCastExpr(RxParser::StatementCastExpressionContext *ctx) {
    auto lhs = buildStatementUnaryExpr(ctx->statementUnaryExpression());
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

NodePtr<Expr> AstBuilder::buildStatementClosedCastExpr(RxParser::StatementClosedCastExpressionContext *ctx) {
    if(ctx->statementUnaryExpression()) {
        return buildStatementUnaryExpr(ctx->statementUnaryExpression());
    }
    auto ret = std::make_unique<CastExpr>(spanOf(ctx));
    ret->type = buildClosedCastType(ctx->closedCastType());
    ret->expr = buildStatementCastExpr(ctx->statementCastExpression());
    return ret;
}

NodePtr<Expr> AstBuilder::buildStatementUnaryExpr(RxParser::StatementUnaryExpressionContext *ctx) {
    if(ctx->statementPostfixExpression()) {
        return buildStatementPostfixExpr(ctx->statementPostfixExpression());
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

NodePtr<Expr> AstBuilder::buildStatementPostfixExpr(RxParser::StatementPostfixExpressionContext *ctx) {
    NodePtr<Expr> ret;
    if(ctx->nonBlockPrimary()) {
        ret = buildNonBlockPrimary(ctx->nonBlockPrimary());
    }
    else {
        ret = buildExpressionWithBlock(ctx->expressionWithBlock());
        auto *dot = ctx->dotSuffix();
        SourceSpan span = spanOf(dot);
        span.beginCol = ret->span.beginCol;
        span.beginLine = ret->span.beginLine;
        if(dot->identifier()) {
            auto field = std::make_unique<FieldExpr>(span);
            field->base = std::move(ret);
            field->field = ident(dot->identifier());
            ret = std::move(field);
        }
        else {
            auto met = std::make_unique<MethodCallExpr>(span);
            met->receiver = std::move(ret);
            PathSegment seg = buildPathExprSegment(dot->pathExprSegment());
            met->method = seg.ident;
            met->methodTypeArgs = std::move(seg.typeArgs);
            for(auto *arg: dot->callArguments()->expression()) {
                met->args.push_back(buildExpr(arg));
            }
            ret = std::move(met);
        }
    }
    for(auto *suffix : ctx->postfixSuffix()) {
        SourceSpan span = spanOf(suffix);
        span.beginCol = ret->span.beginCol;
        span.beginLine = ret->span.beginLine;
        if(suffix->callArguments()) {
            auto call = std::make_unique<CallExpr>(span);
            call->callee = std::move(ret);
            for(auto *arg: suffix->callArguments()->expression()) {
                call->args.push_back(buildExpr(arg));
            }
            ret = std::move(call);
        }
        else if(suffix->LBRACKET() && suffix->RBRACKET()) {
            auto ind = std::make_unique<IndexExpr>(span);
            ind->index = buildExpr(suffix->expression());
            ind->base = std::move(ret);
            ret = std::move(ind);
        }
        else {
            if(suffix->dotSuffix()->identifier()) {
                auto field = std::make_unique<FieldExpr>(span);
                field->base = std::move(ret);
                field->field = ident(suffix->dotSuffix()->identifier());
                ret = std::move(field);
            }
            else {
                auto met = std::make_unique<MethodCallExpr>(span);
                met->receiver = std::move(ret);
                PathSegment seg = buildPathExprSegment(suffix->dotSuffix()->pathExprSegment());
                met->method = seg.ident;
                met->methodTypeArgs = std::move(seg.typeArgs);
                for(auto *arg: suffix->dotSuffix()->callArguments()->expression()) {
                    met->args.push_back(buildExpr(arg));
                }
                ret = std::move(met);
            }
        }
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildPrimaryExpr(RxParser::PrimaryExpressionContext *ctx) {
    if(ctx->nonBlockPrimary()) {
        return buildNonBlockPrimary(ctx->nonBlockPrimary());
    }
    else {
        return buildExpressionWithBlock(ctx->expressionWithBlock());
    }
}

NodePtr<Expr> AstBuilder::buildNonBlockPrimary(RxParser::NonBlockPrimaryContext *ctx) {
    if(ctx->literalExpression()) {
        return buildLiteralExpr(ctx->literalExpression());
    }
    else if(ctx->pathInExpression()) {
        if(ctx->LBRACE()) {
            auto ret = std::make_unique<StructExpr>(spanOf(ctx));
            ret->path = buildPathInExpression(ctx->pathInExpression());
            if(ctx->structExprFields()) {
                ret->fields = buildStructExprFields(ctx->structExprFields());
            }
            return ret;
        }
        else {
            return buildPathInExpression(ctx->pathInExpression());
        }
    }
    else if(ctx->LPAREN() && ctx->RPAREN()) {
        if(ctx->expression()) {//Group (expr)
            auto ret = std::make_unique<GroupExpr>(spanOf(ctx));
            ret->inner = buildExpr(ctx->expression());
            return ret;
        }
        else {
            return std::make_unique<UnitExpr>(spanOf(ctx));
        }
    }
    else if(ctx->arrayExpression()) {
        return buildArrayExpr(ctx->arrayExpression());
    }
    else if(ctx->BREAK()) {
        auto ret = std::make_unique<BreakExpr>(spanOf(ctx));
        ret->val = ctx->expression() ? buildExpr(ctx->expression()) : nullptr;
        return ret;
    }
    else if(ctx->RETURN()) {
        auto ret = std::make_unique<ReturnExpr>(spanOf(ctx));
        ret->val = ctx->expression() ? buildExpr(ctx->expression()) : nullptr;
        return ret;
    }
    else if(ctx->CONTINUE()) {
        return std::make_unique<ContinueExpr>(spanOf(ctx));
    }
    throw std::logic_error("Invalid nonBlockPrimary");
}

NodePtr<Expr> AstBuilder::buildConditionPrimary(RxParser::ConditionPrimaryContext *ctx) {
    if(ctx->conditionPrimaryWithoutBareBlock()) {
        return buildConditionPrimaryWithoutBareBlock(ctx->conditionPrimaryWithoutBareBlock());
    }
    return buildBlockExpr(ctx->blockExpression());
}

NodePtr<Expr> AstBuilder::buildConditionPrimaryWithoutBareBlock(RxParser::ConditionPrimaryWithoutBareBlockContext *ctx) {
    if(ctx->literalExpression()) {
        return buildLiteralExpr(ctx->literalExpression());
    }
    else if(ctx->pathInExpression()) {
        return buildPathInExpression(ctx->pathInExpression());
    }
    else if(ctx->LPAREN() && ctx->RPAREN()) {
        if(ctx->expression()) {//Group (expr)
            auto ret = std::make_unique<GroupExpr>(spanOf(ctx));
            ret->inner = buildExpr(ctx->expression());
            return ret;
        }
        else {
            return std::make_unique<UnitExpr>(spanOf(ctx));
        }
    }
    else if(ctx->arrayExpression()) {
        return buildArrayExpr(ctx->arrayExpression());
    }
    else if(ctx->ifExpression()) {
        return buildIfExpr(ctx->ifExpression());
    }
    else if(ctx->LOOP()) {
        auto ret = std::make_unique<LoopExpr>(spanOf(ctx));
        ret->body = buildBlockExpr(ctx->blockExpression());
        return ret;
    }
    else if(ctx->WHILE()) {
        auto ret = std::make_unique<WhileExpr>(spanOf(ctx));
        ret->cond = buildConditionExpr(ctx->conditionExpression());
        ret->body = buildBlockExpr(ctx->blockExpression());
        return ret;
    }
    else if(ctx->BREAK()) {
        auto ret = std::make_unique<BreakExpr>(spanOf(ctx));
        ret->val = ctx->conditionBreakExpression()
                       ? buildConditionBreakExpr(ctx->conditionBreakExpression()) : nullptr;
        return ret;
    }
    else if(ctx->RETURN()) {
        auto ret = std::make_unique<ReturnExpr>(spanOf(ctx));
        ret->val = ctx->conditionExpression()
                       ? buildConditionExpr(ctx->conditionExpression()) : nullptr;
        return ret;
    }
    else if(ctx->CONTINUE()) {
        return std::make_unique<ContinueExpr>(spanOf(ctx));
    }
    throw std::logic_error("Invalid conditionPrimaryWithoutBareBlock");
}

NodePtr<Stmt> AstBuilder::buildStatement(RxParser::StatementContext *ctx) {
    if(ctx->letStatement()) {
        return buildLetStatement(ctx->letStatement());
    }
    else if(ctx->expressionWithBlock()) {
        auto ret = std::make_unique<ExprStmt>(spanOf(ctx));
        ret->expr = buildExpressionWithBlock(ctx->expressionWithBlock());
        ret->hasSemi = ctx->SEMI() ? true : false;
        return ret;
    }
    else if(ctx->statementExpression()) {
        auto ret = std::make_unique<ExprStmt>(spanOf(ctx));
        ret->expr = buildStatementExpr(ctx->statementExpression());
        ret->hasSemi = true;
        return ret;
    }
    else if(ctx->SEMI()) {
        return std::make_unique<NullStmt>(spanOf(ctx));
    }
    throw std::logic_error("Invalid Stmt");
}

NodePtr<Stmt> AstBuilder::buildLetStatement(RxParser::LetStatementContext *ctx) {
    auto ret = std::make_unique<LetStmt>(spanOf(ctx));
    auto tmp = buildIdentifierBinding(ctx->identifierBinding());
    ret->name = tmp.first;
    ret->isMut = tmp.second;
    ret->type = ctx->COLON() ? buildTypeRef(ctx->typeRef()) : nullptr;
    ret->init = buildExpr(ctx->expression());
    return ret;
}

NodePtr<BlockExpr> AstBuilder::buildBlockExpr(RxParser::BlockExpressionContext *ctx) {
    auto ret = std::make_unique<BlockExpr>(spanOf(ctx));
    if(ctx->statementExpression()) {
        ret->tail = buildStatementExpr(ctx->statementExpression());
    }
    for(auto *sta: ctx->statement()) {
        ret->statements.push_back(buildStatement(sta));
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildExpressionWithBlock(RxParser::ExpressionWithBlockContext *ctx) {
    if(ctx->ifExpression()) {
        return buildIfExpr(ctx->ifExpression());
    }
    else if(ctx->LOOP()) {
        auto ret = std::make_unique<LoopExpr>(spanOf(ctx));
        ret->body = buildBlockExpr(ctx->blockExpression());
        return ret;
    }
    else if(ctx->WHILE()) {
        auto ret = std::make_unique<WhileExpr>(spanOf(ctx));
        ret->cond = buildConditionExpr(ctx->conditionExpression());
        ret->body = buildBlockExpr(ctx->blockExpression());
        return ret;
    }
    else if(ctx->blockExpression()) {
        return buildBlockExpr(ctx->blockExpression());
    }
    throw std::logic_error("Invalid ExpressionWithBlock");
}

NodePtr<Expr> AstBuilder::buildIfExpr(RxParser::IfExpressionContext *ctx) {
    auto ret = std::make_unique<IfExpr>(spanOf(ctx));
    ret->cond = buildConditionExpr(ctx->conditionExpression());
    ret->thenBlock = buildBlockExpr(ctx->blockExpression()[0]);
    if(ctx->ELSE()) {
        if(ctx->ifExpression()) {
            ret->elseBranch = buildIfExpr(ctx->ifExpression());
        }
        else {
            ret->elseBranch = buildBlockExpr(ctx->blockExpression()[1]);
        }
    }
    else {
        ret->elseBranch = nullptr;
    }
    return ret;
}

NodePtr<Expr> AstBuilder::buildLiteralExpr(RxParser::LiteralExpressionContext *ctx) {
    if(ctx->INTEGER_LITERAL()) {
        return std::make_unique<IntExpr>(spanOf(ctx),
                                         parseInteger(ctx->INTEGER_LITERAL()->getSymbol()));
    }
    else if(ctx->TRUE()) {
        return std::make_unique<BoolExpr>(spanOf(ctx), true);
    }
    else if(ctx->FALSE()) {
        return std::make_unique<BoolExpr>(spanOf(ctx), false);
    }
    throw std::logic_error("Invalid LiteralExpr");
}

NodePtr<Expr> AstBuilder::buildArrayExpr(RxParser::ArrayExpressionContext *ctx) {
    auto ret = std::make_unique<ArrayExpr>(spanOf(ctx));
    if(ctx->expression().empty()) {
        ret->elems = std::vector<NodePtr<Expr>>();
    }
    else {
        if(ctx->SEMI()) {
            ret->isRepeat = true;
            ret->repeatLen = buildConstValue(ctx->constValue());
            ret->repeatValue = buildExpr(ctx->expression()[0]);
            ret->elems = std::vector<NodePtr<Expr>>();
        }
        else {
            for(auto *val: ctx->expression()) {
                ret->elems.push_back(buildExpr(val));
            }
        }
    }
    return ret;
}

std::vector<NodePtr<StructExprField>> AstBuilder::buildStructExprFields(RxParser::StructExprFieldsContext *ctx) {
    std::vector<NodePtr<StructExprField>> ret;
    for(auto *field: ctx->structExprField()) {
        ret.push_back(buildStructExprField(field));
    }
    return ret;
}

NodePtr<StructExprField> AstBuilder::buildStructExprField(RxParser::StructExprFieldContext *ctx) {
    auto ret = std::make_unique<StructExprField>(spanOf(ctx));
    ret->name = ident(ctx->identifier());
    ret->value = buildExpr(ctx->expression());
    return ret;
}
