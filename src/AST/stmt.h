#pragma once
#include "AstNode.h"
#include <string>

struct LetStmt : public Stmt {
    std::string name;
    bool isMut = false;
    NodePtr<Type> type;
    NodePtr<Expr> init;
    explicit LetStmt(SourceSpan span) : Stmt(NodeType::LetStmt, span) {}
};

struct ExprStmt : public Stmt {
    NodePtr<Expr> expr;
    bool hasSemi = true;
    explicit ExprStmt(SourceSpan span) : Stmt(NodeType::ExprStmt, span) {}
};

struct NullStmt : public Stmt {
    explicit NullStmt(SourceSpan span) : Stmt(NodeType::NullStmt, span) {}
};
