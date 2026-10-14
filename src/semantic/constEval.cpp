#include "constEval.h"
#include "analyzer.h"
#include <optional>

std::optional<ConstValue> ConstEvaluator::eval(const Expr *expr) {
    switch(expr->typ) {
        case NodeType::GroupExpr: {
            return eval(static_cast<const GroupExpr*>(expr)->inner.get());
        }
        case NodeType::BoolExpr: {
            return std::make_optional<ConstValue>(ConstValue::makeBoolValue(static_cast<const BoolExpr*>(expr)->val));
        }
        case NodeType::IntExpr: {
            return std::make_optional<ConstValue>(ConstValue::makeIntValue(static_cast<const IntExpr*>(expr)->val));
        }
        case NodeType::PathExpr: {
            const Symbol *sym = analyzer->resolveConstPath(static_cast<const PathExpr*>(expr));
            if(sym == nullptr) return std::nullopt;
            auto value = evalConst(sym);
            if(value && sym->type) value->type = sym->type;//a const path has the declared type of its target
            return value;
        }
        case NodeType::UnaryExpr: {
            const auto *unary = static_cast<const UnaryExpr*>(expr);
            if(unary->op != UnaryOp::Neg) {
                analyzer->error(expr->span, "invalid constant expr");
                return std::nullopt;
            }
            auto value = eval(unary->num.get());
            if(!value || !value->isInt()) return std::nullopt;
            SemKind kind = value->type ? value->type->kind : SemKind::I32;
            if(kind != SemKind::I32 && kind != SemKind::Isize) {
                analyzer->error(expr->span, "cannot negate a non-signed value");
                return std::nullopt;
            }
            value->type = SemType::make(kind);
            value->val.val = static_cast<unsigned long long>(-static_cast<long long>(value->val.val));
            return value;
        }
        default: {
            analyzer->error(expr->span, "invalid constant expr");
            break;
        }
    }
    return std::nullopt;
}

std::optional<ConstValue> ConstEvaluator::evalConst(const Symbol *sym) {
    if(sym->kind != SymbolKind::Const && sym->kind != SymbolKind::AssocConst) {
        analyzer->error(sym->span, "not a constant");
        return std::nullopt;
    }

    if(auto it = memo.find(sym); it != memo.end()) {
        return it->second;
    }

    if(failed.count(sym)) {
        return std::nullopt;
    }

    if(visiting.count(sym)) {//circle, invalid
        analyzer->error(sym->span, "cyclic constant");
        return std::nullopt;
    }

    visiting.insert(sym);
    const auto *it = static_cast<const ConstItem*>(sym->decl);
    std::optional<ConstValue> ret = eval(it->value.get());
    visiting.erase(sym);

    if(ret) {
        memo.emplace(sym, *ret);
    }
    else {
        failed.insert(sym);
    }
    return ret;
}
