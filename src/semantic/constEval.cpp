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
            const Symbol *sym = analyzer_->resolveConstPath(static_cast<const PathExpr*>(expr));
            if(sym == nullptr) return std::nullopt;
            return evalConst(sym);
        }
        case NodeType::UnaryExpr: {
            const auto *unary = static_cast<const UnaryExpr*>(expr);
            if(unary->op != UnaryOp::Neg) {
                analyzer_->error(expr->span, "invalid constant expr");
                return std::nullopt;
            }
            auto value = eval(unary->num.get());
            if(!value || !value->isInt()) return std::nullopt;
            if(value->val.suffix == IntSuffix::U32 || value->val.suffix == IntSuffix::Usize) {
                analyzer_->error(expr->span, "cannot negate an unsigned constant");
                return std::nullopt;
            }
            value->val.val = static_cast<unsigned long long>(-static_cast<long long>(value->val.val));
            return value;
        }
        default: {
            analyzer_->error(expr->span, "invalid constant expr");
            break;
        }
    }
    return std::nullopt;
}

std::optional<ConstValue> ConstEvaluator::evalConst(const Symbol *sym) {
    if(sym->kind != SymbolKind::Const && sym->kind != SymbolKind::AssocConst) {
        analyzer_->error(sym->span, "not a constant");
        return std::nullopt;
    }

    if(auto it = memo_.find(sym); it != memo_.end()) {
        return it->second;
    }

    if(failed_.count(sym)) {
        return std::nullopt;
    }

    if(visiting_.count(sym)) {//circle, invalid
        analyzer_->error(sym->span, "cyclic constant");
        return std::nullopt;
    }

    visiting_.insert(sym);
    const auto *it = static_cast<const ConstItem*>(sym->decl);
    std::optional<ConstValue> ret = eval(it->value.get());
    visiting_.erase(sym);

    if(ret) {
        memo_.emplace(sym, *ret);
    }
    else {
        failed_.insert(sym);
    }
    return ret;
}
