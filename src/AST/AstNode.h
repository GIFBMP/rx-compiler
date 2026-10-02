#pragma once

#include <vector>
#include <memory>
#include <string>
#include "antlr4-runtime.h"

struct SourceSpan { 
    size_t beginLine, beginCol, endLine, endCol; 
};

inline SourceSpan spanOf(antlr4::ParserRuleContext* ctx) {
    if (ctx == nullptr || ctx->getStart() == nullptr) {
        return {0, 0, 0, 0};
    }

    antlr4::Token* start = ctx->getStart();
    antlr4::Token* stop = ctx->getStop();
    if (stop == nullptr) {
        stop = start;
    }

    size_t beginLine = start->getLine();
    size_t beginCol = start->getCharPositionInLine();

    std::string text = stop->getText();
    size_t endLine = stop->getLine();
    size_t endCol = stop->getCharPositionInLine();
    size_t lastNewline = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            ++endLine;
            lastNewline = i;
        }
    }
    if (lastNewline == std::string::npos) {
        endCol += text.size();
    } else {
        endCol = text.size() - lastNewline - 1;
    }

    return {beginLine, beginCol, endLine, endCol};
}

enum class NodeType {
    Crate,

    UseDecItem,
    FuncDefItem,
    StructDefItem,
    ConstItem,
    ImplItem,

    LetStmt,
    ExprStmt,
    NullStmt, //single ";" is it necessary?

    BlockExpr,

    IntExpr,
    BoolExpr, //literal expressions

    IfExpr,
    WhileExpr,
    LoopExpr, //control flow

    StructExpr,
    StructExprField,
    ArrayExpr,
    IndexExpr,
    CallExpr,
    MethodCallExpr,
    AssignExpr,
    FieldExpr, //A.B.C
    PathExpr, //A::B::C (Impl call)

    CastExpr, // ... as ...

    UnitExpr,
    GroupExpr,

    UnaryExpr,
    BinaryExpr, // +, -, *, <<, >>, etc.

    ReturnExpr,
    BreakExpr,
    ContinueExpr,

    ArrayType,
    RefType,
    UnitType,
    PathType,
    ParenthesizedType,

    FuncParam,
    StructField,

};

template<class T>
using NodePtr = std::unique_ptr<T>;

struct AstNode {
    NodeType typ;
    SourceSpan span;

    explicit AstNode(NodeType typ, SourceSpan span) : typ(typ), span(span) {}
    virtual ~AstNode() = default;
};

using AstPtr = std::unique_ptr<AstNode>;

struct Item : public AstNode {using AstNode::AstNode;};
struct Stmt : public AstNode {using AstNode::AstNode;};
struct Type : public AstNode {using AstNode::AstNode;};
struct Expr : public AstNode {using AstNode::AstNode;};

struct PathSegment {
    std::string ident;
    bool isSelf     = false;
    bool isSelfType = false;
    std::vector<NodePtr<Type>> typeArgs;
};

struct Crate : public AstNode {
    std::vector<NodePtr<Item>> items; 

    explicit Crate(SourceSpan span) : AstNode(NodeType::Crate, span) {}
};

enum class UnaryOp {Neg, Not, Deref, Ref, RefMut}; //-x !x *x &x &mut x

enum class BinaryOp {
    Add, Sub, Mul, Div, Rem,        //+ - * / %
    BitAnd, BitOr, BitXor, Shl, Shr,//& | ^ << >>
    Eq, Ne, Lt, Le, Gt, Ge,         //== != < <= > >=
    And, Or                         //&& ||
};

enum class AssignOp {
    Assign,                        // =
    Add, Sub, Mul, Div, Rem,       // += -= *= /= %=
    BitAnd, BitOr, BitXor, Shl, Shr// &= |= ^= <<= >>=
};

enum class Derive {Copy, Clone, PartialEq, Eq};