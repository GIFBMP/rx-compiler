#pragma once
#include "AstNode.h"
#include "expr.h"
#include "item.h"
#include "stmt.h"
#include "type.h"
#include <ostream>
#include <sstream>
#include <string>

namespace astdump {

inline std::string spanStr(const SourceSpan &s) {
    std::ostringstream os;
    os << "[" << s.beginLine << ":" << s.beginCol << "-"
       << s.endLine << ":" << s.endCol << "]";
    return os.str();
}

inline const char *nodeName(NodeType t) {
    switch(t) {
        case NodeType::Crate:              return "Crate";
        case NodeType::UseDecItem:         return "UseDecItem";
        case NodeType::FuncDefItem:        return "FuncDefItem";
        case NodeType::StructDefItem:      return "StructDefItem";
        case NodeType::ConstItem:          return "ConstItem";
        case NodeType::ImplItem:           return "ImplItem";
        case NodeType::LetStmt:            return "LetStmt";
        case NodeType::ExprStmt:           return "ExprStmt";
        case NodeType::NullStmt:           return "NullStmt";
        case NodeType::BlockExpr:          return "BlockExpr";
        case NodeType::IntExpr:            return "IntExpr";
        case NodeType::BoolExpr:           return "BoolExpr";
        case NodeType::IfExpr:             return "IfExpr";
        case NodeType::WhileExpr:          return "WhileExpr";
        case NodeType::LoopExpr:           return "LoopExpr";
        case NodeType::StructExpr:         return "StructExpr";
        case NodeType::StructExprField:    return "StructExprField";
        case NodeType::ArrayExpr:          return "ArrayExpr";
        case NodeType::IndexExpr:          return "IndexExpr";
        case NodeType::CallExpr:           return "CallExpr";
        case NodeType::MethodCallExpr:     return "MethodCallExpr";
        case NodeType::AssignExpr:         return "AssignExpr";
        case NodeType::FieldExpr:          return "FieldExpr";
        case NodeType::PathExpr:           return "PathExpr";
        case NodeType::CastExpr:           return "CastExpr";
        case NodeType::UnitExpr:           return "UnitExpr";
        case NodeType::GroupExpr:          return "GroupExpr";
        case NodeType::UnaryExpr:          return "UnaryExpr";
        case NodeType::BinaryExpr:         return "BinaryExpr";
        case NodeType::ReturnExpr:         return "ReturnExpr";
        case NodeType::BreakExpr:          return "BreakExpr";
        case NodeType::ContinueExpr:       return "ContinueExpr";
        case NodeType::ArrayType:          return "ArrayType";
        case NodeType::RefType:            return "RefType";
        case NodeType::UnitType:           return "UnitType";
        case NodeType::PathType:           return "PathType";
        case NodeType::ParenthesizedType:  return "ParenthesizedType";
        case NodeType::FuncParam:          return "FuncParam";
        case NodeType::StructField:        return "StructField";
    }
    return "Unknown";
}

inline const char *unaryOpName(UnaryOp op) {
    switch(op) {
        case UnaryOp::Neg:    return "Neg";
        case UnaryOp::Not:    return "Not";
        case UnaryOp::Deref:  return "Deref";
        case UnaryOp::Ref:    return "Ref";
        case UnaryOp::RefMut: return "RefMut";
    }
    return "?";
}

inline const char *binaryOpName(BinaryOp op) {
    switch(op) {
        case BinaryOp::Add:        return "Add";
        case BinaryOp::Sub:        return "Sub";
        case BinaryOp::Mul:        return "Mul";
        case BinaryOp::Div:        return "Div";
        case BinaryOp::Mod:        return "Mod";
        case BinaryOp::BitAnd:     return "BitAnd";
        case BinaryOp::BitOr:      return "BitOr";
        case BinaryOp::BitXor:     return "BitXor";
        case BinaryOp::Shl:        return "Shl";
        case BinaryOp::Shr:        return "Shr";
        case BinaryOp::Eq:         return "Eq";
        case BinaryOp::Ne:         return "Ne";
        case BinaryOp::Lt:         return "Lt";
        case BinaryOp::Le:         return "Le";
        case BinaryOp::Gt:         return "Gt";
        case BinaryOp::Ge:         return "Ge";
        case BinaryOp::LogicalAnd: return "LogicalAnd";
        case BinaryOp::LogicalOr:  return "LogicalOr";
    }
    return "?";
}

inline const char *assignOpName(AssignOp op) {
    switch(op) {
        case AssignOp::Assign: return "Assign";
        case AssignOp::Add:    return "Add";
        case AssignOp::Sub:    return "Sub";
        case AssignOp::Mul:    return "Mul";
        case AssignOp::Div:    return "Div";
        case AssignOp::Mod:    return "Mod";
        case AssignOp::BitAnd: return "BitAnd";
        case AssignOp::BitOr:  return "BitOr";
        case AssignOp::BitXor: return "BitXor";
        case AssignOp::Shl:    return "Shl";
        case AssignOp::Shr:    return "Shr";
    }
    return "?";
}

inline const char *deriveName(Derive d) {
    switch(d) {
        case Derive::Copy:      return "Copy";
        case Derive::Clone:     return "Clone";
        case Derive::PartialEq: return "PartialEq";
        case Derive::Eq:        return "Eq";
    }
    return "?";
}

inline const char *intSuffixName(IntSuffix s) {
    switch(s) {
        case IntSuffix::Infer: return "Infer";
        case IntSuffix::I32:   return "i32";
        case IntSuffix::U32:   return "u32";
        case IntSuffix::Isize: return "isize";
        case IntSuffix::Usize: return "usize";
    }
    return "?";
}

inline void pad(std::ostream &os, int indent) {
    for(int i = 0; i < indent; i++) {
        os << "  ";
    }
}

inline std::string segName(const PathSegment &seg) {
    std::string s = seg.ident;
    if(s.empty()) {
        s = seg.isSelf ? "self" : (seg.isSelfType ? "Self" : "?");
    }
    if(!seg.typeArgs.empty()) {
        s += "<" + std::to_string(seg.typeArgs.size()) + ">";
    }
    return s;
}

inline void dump(const AstNode *node, std::ostream &os, int indent) {
    if(node == nullptr) {
        pad(os, indent);
        os << "<null>\n";
        return;
    }
    pad(os, indent);
    os << nodeName(node->typ) << " " << spanStr(node->span);

    switch(node->typ) {
        case NodeType::Crate: {
            auto *n = static_cast<const Crate *>(node);
            os << " items=" << n->items.size() << "\n";
            for(auto &it : n->items) {
                dump(it.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::UseDecItem: {
            auto *n = static_cast<const UseDecItem *>(node);
            os << " raw=\"" << n->raw << "\"\n";
            break;
        }
        case NodeType::FuncDefItem: {
            auto *n = static_cast<const FuncDefItem *>(node);
            os << " name=" << n->name << " params=" << n->params.size() << "\n";
            for(auto &p : n->params) {
                dump(p.get(), os, indent + 1);
            }
            if(n->returnType) {
                pad(os, indent + 1);
                os << "returnType:\n";
                dump(n->returnType.get(), os, indent + 2);
            }
            if(n->body) {
                dump(n->body.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::StructDefItem: {
            auto *n = static_cast<const StructDefItem *>(node);
            os << " name=" << n->name << " derives=[";
            for(size_t i = 0; i < n->derives.size(); i++) {
                if(i) os << ",";
                os << deriveName(n->derives[i]);
            }
            os << "]\n";
            for(auto &f : n->fields) {
                dump(f.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::ConstItem: {
            auto *n = static_cast<const ConstItem *>(node);
            os << " name=" << n->name << "\n";
            if(n->type) dump(n->type.get(), os, indent + 1);
            if(n->value) dump(n->value.get(), os, indent + 1);
            break;
        }
        case NodeType::ImplItem: {
            auto *n = static_cast<const ImplItem *>(node);
            os << " associated=" << n->items.size() << "\n";
            if(n->selfType) dump(n->selfType.get(), os, indent + 1);
            for(auto &it : n->items) {
                dump(it.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::LetStmt: {
            auto *n = static_cast<const LetStmt *>(node);
            os << " name=" << n->name << " mut=" << (n->isMut ? "true" : "false") << "\n";
            if(n->type) dump(n->type.get(), os, indent + 1);
            if(n->init) dump(n->init.get(), os, indent + 1);
            break;
        }
        case NodeType::ExprStmt: {
            auto *n = static_cast<const ExprStmt *>(node);
            os << " hasSemi=" << (n->hasSemi ? "true" : "false") << "\n";
            if(n->expr) dump(n->expr.get(), os, indent + 1);
            break;
        }
        case NodeType::NullStmt: {
            os << "\n";
            break;
        }
        case NodeType::BlockExpr: {
            auto *n = static_cast<const BlockExpr *>(node);
            os << " statements=" << n->statements.size() << "\n";
            for(auto &s : n->statements) {
                dump(s.get(), os, indent + 1);
            }
            if(n->tail) {
                pad(os, indent + 1);
                os << "tail:\n";
                dump(n->tail.get(), os, indent + 2);
            }
            break;
        }
        case NodeType::IntExpr: {
            auto *n = static_cast<const IntExpr *>(node);
            os << " literal=" << n->val.literal << " val=" << n->val.val
               << " suffix=" << intSuffixName(n->val.suffix) << "\n";
            break;
        }
        case NodeType::BoolExpr: {
            auto *n = static_cast<const BoolExpr *>(node);
            os << " val=" << (n->val ? "true" : "false") << "\n";
            break;
        }
        case NodeType::IfExpr: {
            auto *n = static_cast<const IfExpr *>(node);
            os << "\n";
            pad(os, indent + 1);
            os << "cond:\n";
            dump(n->cond.get(), os, indent + 2);
            pad(os, indent + 1);
            os << "then:\n";
            dump(n->thenBlock.get(), os, indent + 2);
            if(n->elseBranch) {
                pad(os, indent + 1);
                os << "else:\n";
                dump(n->elseBranch.get(), os, indent + 2);
            }
            break;
        }
        case NodeType::WhileExpr: {
            auto *n = static_cast<const WhileExpr *>(node);
            os << "\n";
            pad(os, indent + 1);
            os << "cond:\n";
            dump(n->cond.get(), os, indent + 2);
            pad(os, indent + 1);
            os << "body:\n";
            dump(n->body.get(), os, indent + 2);
            break;
        }
        case NodeType::LoopExpr: {
            auto *n = static_cast<const LoopExpr *>(node);
            os << "\n";
            dump(n->body.get(), os, indent + 1);
            break;
        }
        case NodeType::StructExpr: {
            auto *n = static_cast<const StructExpr *>(node);
            os << " fields=" << n->fields.size() << "\n";
            if(n->path) dump(n->path.get(), os, indent + 1);
            for(auto &f : n->fields) {
                dump(f.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::StructExprField: {
            auto *n = static_cast<const StructExprField *>(node);
            os << " name=" << n->name << "\n";
            if(n->value) dump(n->value.get(), os, indent + 1);
            break;
        }
        case NodeType::ArrayExpr: {
            auto *n = static_cast<const ArrayExpr *>(node);
            os << " isRepeat=" << (n->isRepeat ? "true" : "false")
               << " elems=" << n->elems.size() << "\n";
            for(auto &e : n->elems) {
                dump(e.get(), os, indent + 1);
            }
            if(n->repeatValue) dump(n->repeatValue.get(), os, indent + 1);
            if(n->repeatLen) dump(n->repeatLen.get(), os, indent + 1);
            break;
        }
        case NodeType::IndexExpr: {
            auto *n = static_cast<const IndexExpr *>(node);
            os << "\n";
            if(n->base) dump(n->base.get(), os, indent + 1);
            if(n->index) dump(n->index.get(), os, indent + 1);
            break;
        }
        case NodeType::CallExpr: {
            auto *n = static_cast<const CallExpr *>(node);
            os << " args=" << n->args.size() << "\n";
            if(n->callee) dump(n->callee.get(), os, indent + 1);
            for(auto &a : n->args) {
                dump(a.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::MethodCallExpr: {
            auto *n = static_cast<const MethodCallExpr *>(node);
            os << " method=" << n->method << " args=" << n->args.size()
               << " typeArgs=" << n->methodTypeArgs.size() << "\n";
            if(n->receiver) dump(n->receiver.get(), os, indent + 1);
            for(auto &t : n->methodTypeArgs) {
                dump(t.get(), os, indent + 1);
            }
            for(auto &a : n->args) {
                dump(a.get(), os, indent + 1);
            }
            break;
        }
        case NodeType::AssignExpr: {
            auto *n = static_cast<const AssignExpr *>(node);
            os << " op=" << assignOpName(n->op) << "\n";
            if(n->lhs) dump(n->lhs.get(), os, indent + 1);
            if(n->rhs) dump(n->rhs.get(), os, indent + 1);
            break;
        }
        case NodeType::FieldExpr: {
            auto *n = static_cast<const FieldExpr *>(node);
            os << " field=" << n->field << "\n";
            if(n->base) dump(n->base.get(), os, indent + 1);
            break;
        }
        case NodeType::PathExpr: {
            auto *n = static_cast<const PathExpr *>(node);
            os << " segments=" << n->segments.size() << " [";
            for(size_t i = 0; i < n->segments.size(); i++) {
                if(i) os << "::";
                os << segName(n->segments[i]);
            }
            os << "]\n";
            for(auto &s : n->segments) {
                for(auto &t : s.typeArgs) {
                    dump(t.get(), os, indent + 1);
                }
            }
            break;
        }
        case NodeType::CastExpr: {
            auto *n = static_cast<const CastExpr *>(node);
            os << "\n";
            if(n->expr) dump(n->expr.get(), os, indent + 1);
            if(n->type) dump(n->type.get(), os, indent + 1);
            break;
        }
        case NodeType::UnitExpr: {
            os << "\n";
            break;
        }
        case NodeType::GroupExpr: {
            auto *n = static_cast<const GroupExpr *>(node);
            os << "\n";
            if(n->inner) dump(n->inner.get(), os, indent + 1);
            break;
        }
        case NodeType::UnaryExpr: {
            auto *n = static_cast<const UnaryExpr *>(node);
            os << " op=" << unaryOpName(n->op) << "\n";
            if(n->num) dump(n->num.get(), os, indent + 1);
            break;
        }
        case NodeType::BinaryExpr: {
            auto *n = static_cast<const BinaryExpr *>(node);
            os << " op=" << binaryOpName(n->op) << "\n";
            if(n->lhs) dump(n->lhs.get(), os, indent + 1);
            if(n->rhs) dump(n->rhs.get(), os, indent + 1);
            break;
        }
        case NodeType::ReturnExpr: {
            auto *n = static_cast<const ReturnExpr *>(node);
            os << "\n";
            if(n->val) dump(n->val.get(), os, indent + 1);
            break;
        }
        case NodeType::BreakExpr: {
            auto *n = static_cast<const BreakExpr *>(node);
            os << "\n";
            if(n->val) dump(n->val.get(), os, indent + 1);
            break;
        }
        case NodeType::ContinueExpr: {
            os << "\n";
            break;
        }
        case NodeType::ArrayType: {
            auto *n = static_cast<const ArrayType *>(node);
            os << "\n";
            if(n->elem) dump(n->elem.get(), os, indent + 1);
            if(n->len) dump(n->len.get(), os, indent + 1);
            break;
        }
        case NodeType::RefType: {
            auto *n = static_cast<const RefType *>(node);
            os << " mut=" << (n->isMut ? "true" : "false") << "\n";
            if(n->referent) dump(n->referent.get(), os, indent + 1);
            break;
        }
        case NodeType::UnitType: {
            os << "\n";
            break;
        }
        case NodeType::PathType: {
            auto *n = static_cast<const PathType *>(node);
            os << " segments=" << n->segments.size() << " [";
            for(size_t i = 0; i < n->segments.size(); i++) {
                if(i) os << "::";
                os << segName(n->segments[i]);
            }
            os << "]\n";
            for(auto &s : n->segments) {
                for(auto &t : s.typeArgs) {
                    dump(t.get(), os, indent + 1);
                }
            }
            break;
        }
        case NodeType::ParenthesizedType: {
            auto *n = static_cast<const ParenthesizedType *>(node);
            os << "\n";
            if(n->inner) dump(n->inner.get(), os, indent + 1);
            break;
        }
        case NodeType::FuncParam: {
            auto *n = static_cast<const FuncParam *>(node);
            os << " name=\"" << n->name << "\" self=" << (n->isSelf ? "true" : "false")
               << " mut=" << (n->isMut ? "true" : "false")
               << " byRef=" << (n->byRef ? "true" : "false") << "\n";
            if(n->type) dump(n->type.get(), os, indent + 1);
            break;
        }
        case NodeType::StructField: {
            auto *n = static_cast<const StructField *>(node);
            os << " name=" << n->name << "\n";
            if(n->type) dump(n->type.get(), os, indent + 1);
            break;
        }
    }
}

inline void dump(const AstNode *node, std::ostream &os) {
    dump(node, os, 0);
}

} // namespace astdump
