#pragma once
#include "AstNode.h"
#include <string>
#include <vector>

struct BoolExpr : public Expr {
    bool val = false;
    explicit BoolExpr(SourceSpan span, bool val) : Expr(NodeType::BoolExpr, span), val(val) {}
};

enum class IntSuffix {Infer, I32, U32, Isize, Usize};

struct IntValue {
    std::string literal;
    unsigned long long val = 0;
    IntSuffix suffix = IntSuffix::Infer;
};

struct IntExpr : public Expr {
    IntValue val;
    explicit IntExpr(SourceSpan span, IntValue val) : Expr(NodeType::IntExpr, span), val(val) {}
};

struct BlockExpr : public Expr {
    std::vector<NodePtr<Stmt>> statements;
    NodePtr<Expr> tail;
    explicit BlockExpr(SourceSpan span) : Expr(NodeType::BlockExpr, span) {}
};

struct PathExpr : public Expr {
    std::vector<PathSegment> segments;
    explicit PathExpr(SourceSpan span) : Expr(NodeType::PathExpr, span) {}
};

struct UnaryExpr : public Expr {
    UnaryOp op;
    NodePtr<Expr> num;
    explicit UnaryExpr(SourceSpan span) : Expr(NodeType::UnaryExpr, span) {}
};

struct BinaryExpr : public Expr {
    BinaryOp op;
    NodePtr<Expr> lhs, rhs;
    explicit BinaryExpr(SourceSpan span) : Expr(NodeType::BinaryExpr, span) {}
};

struct AssignExpr : public Expr {
    AssignOp op;
    NodePtr<Expr> lhs, rhs;
    explicit AssignExpr(SourceSpan span) : Expr(NodeType::AssignExpr, span) {}
};

struct FieldExpr : public Expr {
    NodePtr<Expr> base;
    std::string field;
    explicit FieldExpr(SourceSpan span) : Expr(NodeType::FieldExpr, span) {}
};

struct ReturnExpr : public Expr {
    NodePtr<Expr> val;
    explicit ReturnExpr(SourceSpan span) : Expr(NodeType::ReturnExpr, span) {}
};

struct BreakExpr : public Expr {
    NodePtr<Expr> val;
    explicit BreakExpr(SourceSpan span) : Expr(NodeType::BreakExpr, span) {}
};

struct ContinueExpr : public Expr {
    explicit ContinueExpr(SourceSpan span) : Expr(NodeType::ContinueExpr, span) {}
};

struct IfExpr : public Expr {
    NodePtr<Expr> cond;
    NodePtr<BlockExpr> thenBlock;
    NodePtr<Expr> elseBranch;
    explicit IfExpr(SourceSpan span) : Expr(NodeType::IfExpr, span) {}
};

struct WhileExpr : public Expr {
    NodePtr<Expr> cond;
    NodePtr<BlockExpr> body;
    explicit WhileExpr(SourceSpan span) : Expr(NodeType::WhileExpr, span) {}
};

struct LoopExpr : public Expr {
    NodePtr<BlockExpr> body;
    explicit LoopExpr(SourceSpan span) : Expr(NodeType::LoopExpr, span) {}
};

struct StructExprField : public AstNode {
    std::string name;
    NodePtr<Expr> value;
    explicit StructExprField(SourceSpan span) : AstNode(NodeType::StructExprField, span) {}
};

struct StructExpr : public Expr {
    NodePtr<PathExpr> path;
    std::vector<NodePtr<StructExprField>> fields;
    explicit StructExpr(SourceSpan span) : Expr(NodeType::StructExpr, span) {}
};

struct ArrayExpr : public Expr {
    bool isRepeat = false;
    std::vector<NodePtr<Expr>> elems;
    NodePtr<Expr> repeatValue;
    NodePtr<Expr> repeatLen;
    explicit ArrayExpr(SourceSpan span) : Expr(NodeType::ArrayExpr, span) {}
};

struct IndexExpr : public Expr {
    NodePtr<Expr> base;
    NodePtr<Expr> index;
    explicit IndexExpr(SourceSpan span) : Expr(NodeType::IndexExpr, span) {}
};

struct CallExpr : public Expr {
    NodePtr<Expr> callee;
    std::vector<NodePtr<Expr>> args;
    explicit CallExpr(SourceSpan span) : Expr(NodeType::CallExpr, span) {}
};

struct MethodCallExpr : public Expr {
    NodePtr<Expr> receiver;
    std::string method;
    std::vector<NodePtr<Type>> methodTypeArgs;
    std::vector<NodePtr<Expr>> args;
    explicit MethodCallExpr(SourceSpan span) : Expr(NodeType::MethodCallExpr, span) {}
};

struct CastExpr : public Expr {
    NodePtr<Expr> expr;
    NodePtr<Type> type;
    explicit CastExpr(SourceSpan span) : Expr(NodeType::CastExpr, span) {}
};

struct UnitExpr : public Expr {
    explicit UnitExpr(SourceSpan span) : Expr(NodeType::UnitExpr, span) {}
};

struct GroupExpr : public Expr {
    NodePtr<Expr> inner;
    explicit GroupExpr(SourceSpan span) : Expr(NodeType::GroupExpr, span) {}
};
