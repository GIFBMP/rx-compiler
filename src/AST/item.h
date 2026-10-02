#pragma once
#include "AstNode.h"
#include "expr.h"
#include <string>
#include <vector>

struct UseDecItem : public Item {
    std::string raw;
    explicit UseDecItem(SourceSpan span, std::string raw)
        : Item(NodeType::UseDecItem, span), raw(std::move(raw)) {}
};

struct FuncParam : public AstNode {
    std::string name;
    NodePtr<Type> type;
    bool isMut  = false;
    bool isSelf = false;
    bool byRef  = false;
    explicit FuncParam(SourceSpan span) : AstNode(NodeType::FuncParam, span) {}
};

struct FuncDefItem : public Item {
    std::string name;
    std::vector<NodePtr<FuncParam>> params;
    NodePtr<Type> returnType;
    NodePtr<BlockExpr> body;
    explicit FuncDefItem(SourceSpan span) : Item(NodeType::FuncDefItem, span) {}
};

struct StructField : public AstNode {
    std::string name;
    NodePtr<Type> type;
    explicit StructField(SourceSpan span) : AstNode(NodeType::StructField, span) {}
};

struct StructDefItem : public Item {
    std::string name;
    std::vector<NodePtr<StructField>> fields;
    std::vector<Derive> derives;
    explicit StructDefItem(SourceSpan span) : Item(NodeType::StructDefItem, span) {}
};

struct ConstItem : public Item {
    std::string name;
    NodePtr<Type> type;
    NodePtr<Expr> value;
    explicit ConstItem(SourceSpan span) : Item(NodeType::ConstItem, span) {}
};

struct ImplItem : public Item {
    NodePtr<Type> selfType;
    std::vector<NodePtr<Item>> items;
    explicit ImplItem(SourceSpan span) : Item(NodeType::ImplItem, span) {}
};
