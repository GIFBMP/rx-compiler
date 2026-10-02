#pragma once
#include "AstNode.h"
#include <vector>

struct PathType : public Type {
    std::vector<PathSegment> segments;
    explicit PathType(SourceSpan span) : Type(NodeType::PathType, span) {}
};

struct RefType : public Type {
    NodePtr<Type> referent;
    bool isMut = false;
    explicit RefType(SourceSpan span) : Type(NodeType::RefType, span) {}
};

struct ArrayType : public Type {
    NodePtr<Type> elem;
    NodePtr<Expr> len;
    explicit ArrayType(SourceSpan span) : Type(NodeType::ArrayType, span) {}
};

struct UnitType : public Type {
    explicit UnitType(SourceSpan span) : Type(NodeType::UnitType, span) {}
};

struct ParenthesizedType : public Type {
    NodePtr<Type> inner;
    explicit ParenthesizedType(SourceSpan span) : Type(NodeType::ParenthesizedType, span) {}
};
