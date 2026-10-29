#pragma once
#include "AstDumper.h"
#include "AstNode.h"
#include "expr.h"
#include "item.h"
#include "stmt.h"
#include "type.h"
#include <string>
#include <vector>

namespace rx::ast {
namespace astcheck {

inline void check(const AstNode *node, std::vector<std::string> &errors) {
    if(node == nullptr) {
        errors.push_back("<null node>");
        return;
    }

    auto where = [&]() {
        return std::string(astdump::nodeName(node->typ)) + " @" + astdump::spanStr(node->span);
    };
    auto need = [&](const AstNode *child, const char *field) {
        if(child == nullptr) {
            errors.push_back(where() + ": null " + field);
        }
        else {
            check(child, errors);
        }
    };
    auto needList = [&](const auto &list, const char *field) {
        for(const auto &item : list) {
            need(item.get(), field);
        }
    };
    auto checkSegments = [&](const std::vector<PathSegment> &segments, const char *field) {
        for(const auto &seg : segments) {
            for(const auto &arg : seg.typeArgs) {
                need(arg.get(), field);
            }
        }
    };

    // span must be non-decreasing
    const auto &s = node->span;
    if(s.beginLine > s.endLine || (s.beginLine == s.endLine && s.beginCol > s.endCol)) {
        errors.push_back(where() + ": inverted span");
    }

    switch(node->typ) {
        case NodeType::Crate: {
            auto *x = static_cast<const Crate *>(node);
            needList(x->items, "item");
            break;
        }
        case NodeType::UseDecItem:
            break;
        case NodeType::FuncDefItem: {
            auto *x = static_cast<const FuncDefItem *>(node);
            needList(x->params, "param");
            if(x->returnType) check(x->returnType.get(), errors);
            need(x->body.get(), "body");
            break;
        }
        case NodeType::FuncParam: {
            auto *x = static_cast<const FuncParam *>(node);
            if(!x->isSelf) need(x->type.get(), "type");
            break;
        }
        case NodeType::StructDefItem: {
            auto *x = static_cast<const StructDefItem *>(node);
            needList(x->fields, "field");
            break;
        }
        case NodeType::StructField: {
            auto *x = static_cast<const StructField *>(node);
            need(x->type.get(), "type");
            break;
        }
        case NodeType::ConstItem: {
            auto *x = static_cast<const ConstItem *>(node);
            need(x->type.get(), "type");
            need(x->value.get(), "value");
            break;
        }
        case NodeType::ImplItem: {
            auto *x = static_cast<const ImplItem *>(node);
            need(x->selfType.get(), "selfType");
            for(const auto &it : x->items) {
                need(it.get(), "associatedItem");
                if(it && it->typ != NodeType::ConstItem && it->typ != NodeType::FuncDefItem) {
                    errors.push_back(where() + ": ImplItem holds non-associated node");
                }
            }
            break;
        }
        case NodeType::LetStmt: {
            auto *x = static_cast<const LetStmt *>(node);
            if(x->type) check(x->type.get(), errors);
            need(x->init.get(), "init");
            break;
        }
        case NodeType::ExprStmt: {
            auto *x = static_cast<const ExprStmt *>(node);
            need(x->expr.get(), "expr");
            break;
        }
        case NodeType::NullStmt:
            break;
        case NodeType::BlockExpr: {
            auto *x = static_cast<const BlockExpr *>(node);
            needList(x->statements, "statement");
            if(x->tail) check(x->tail.get(), errors);
            break;
        }
        case NodeType::IntExpr:
        case NodeType::BoolExpr:
        case NodeType::UnitExpr:
        case NodeType::ContinueExpr:
        case NodeType::UnitType:
            break;
        case NodeType::IfExpr: {
            auto *x = static_cast<const IfExpr *>(node);
            need(x->cond.get(), "cond");
            need(x->thenBlock.get(), "thenBlock");
            if(x->elseBranch) check(x->elseBranch.get(), errors);
            break;
        }
        case NodeType::WhileExpr: {
            auto *x = static_cast<const WhileExpr *>(node);
            need(x->cond.get(), "cond");
            need(x->body.get(), "body");
            break;
        }
        case NodeType::LoopExpr: {
            auto *x = static_cast<const LoopExpr *>(node);
            need(x->body.get(), "body");
            break;
        }
        case NodeType::StructExpr: {
            auto *x = static_cast<const StructExpr *>(node);
            need(x->path.get(), "path");
            needList(x->fields, "field");
            break;
        }
        case NodeType::StructExprField: {
            auto *x = static_cast<const StructExprField *>(node);
            need(x->value.get(), "value");
            break;
        }
        case NodeType::ArrayExpr: {
            auto *x = static_cast<const ArrayExpr *>(node);
            if(x->isRepeat) {
                need(x->repeatValue.get(), "repeatValue");
                need(x->repeatLen.get(), "repeatLen");
            }
            else {
                needList(x->elems, "elem");
            }
            break;
        }
        case NodeType::IndexExpr: {
            auto *x = static_cast<const IndexExpr *>(node);
            need(x->base.get(), "base");
            need(x->index.get(), "index");
            break;
        }
        case NodeType::CallExpr: {
            auto *x = static_cast<const CallExpr *>(node);
            need(x->callee.get(), "callee");
            needList(x->args, "arg");
            break;
        }
        case NodeType::MethodCallExpr: {
            auto *x = static_cast<const MethodCallExpr *>(node);
            need(x->receiver.get(), "receiver");
            needList(x->methodTypeArgs, "methodTypeArg");
            needList(x->args, "arg");
            break;
        }
        case NodeType::AssignExpr: {
            auto *x = static_cast<const AssignExpr *>(node);
            need(x->lhs.get(), "lhs");
            need(x->rhs.get(), "rhs");
            break;
        }
        case NodeType::FieldExpr: {
            auto *x = static_cast<const FieldExpr *>(node);
            need(x->base.get(), "base");
            break;
        }
        case NodeType::PathExpr: {
            auto *x = static_cast<const PathExpr *>(node);
            if(x->segments.empty()) errors.push_back(where() + ": empty path");
            checkSegments(x->segments, "pathTypeArg");
            break;
        }
        case NodeType::CastExpr: {
            auto *x = static_cast<const CastExpr *>(node);
            need(x->expr.get(), "expr");
            need(x->type.get(), "type");
            break;
        }
        case NodeType::GroupExpr: {
            auto *x = static_cast<const GroupExpr *>(node);
            need(x->inner.get(), "inner");
            break;
        }
        case NodeType::UnaryExpr: {
            auto *x = static_cast<const UnaryExpr *>(node);
            need(x->num.get(), "num");
            break;
        }
        case NodeType::BinaryExpr: {
            auto *x = static_cast<const BinaryExpr *>(node);
            need(x->lhs.get(), "lhs");
            need(x->rhs.get(), "rhs");
            break;
        }
        case NodeType::ReturnExpr: {
            auto *x = static_cast<const ReturnExpr *>(node);
            if(x->val) check(x->val.get(), errors);
            break;
        }
        case NodeType::BreakExpr: {
            auto *x = static_cast<const BreakExpr *>(node);
            if(x->val) check(x->val.get(), errors);
            break;
        }
        case NodeType::ArrayType: {
            auto *x = static_cast<const ArrayType *>(node);
            need(x->elem.get(), "elem");
            need(x->len.get(), "len");
            break;
        }
        case NodeType::RefType: {
            auto *x = static_cast<const RefType *>(node);
            need(x->referent.get(), "referent");
            break;
        }
        case NodeType::PathType: {
            auto *x = static_cast<const PathType *>(node);
            if(x->segments.empty()) errors.push_back(where() + ": empty path");
            checkSegments(x->segments, "pathTypeArg");
            break;
        }
        case NodeType::ParenthesizedType: {
            auto *x = static_cast<const ParenthesizedType *>(node);
            need(x->inner.get(), "inner");
            break;
        }
    }
}

inline std::vector<std::string> check(const AstNode *root) {
    std::vector<std::string> errors;
    check(root, errors);
    return errors;
}

} // namespace astcheck
} // namespace rx::ast
