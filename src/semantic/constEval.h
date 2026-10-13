#pragma once
#include "../AST/AstBuilder.h"
#include <optional>
#include <unordered_map>
#include <unordered_set>

struct Symbol;
struct PathExpr;
class SemanticAnalyzer;

enum class ConstKind {
    Int, Bool
};

struct ConstValue {
    ConstKind kind = ConstKind::Int;

    IntValue val;

    bool boolVal = false;

    static ConstValue makeIntValue(const IntValue &v) {
        return ConstValue{ConstKind::Int, v, false};
    }

    static ConstValue makeBoolValue(bool b) {
        return ConstValue{ConstKind::Bool, IntValue{}, b};
    }

    bool isInt() const { return kind == ConstKind::Int; }
    bool isBool() const { return kind == ConstKind::Bool; }
};

class ConstEvaluator {
public:
    void setAnalyzer(SemanticAnalyzer *a) { analyzer = a; }

    std::optional<ConstValue> eval(const Expr *expr);
    std::optional<ConstValue> evalConst(const Symbol *sym);

private:
    SemanticAnalyzer *analyzer = nullptr;

    std::unordered_map<const Symbol *, ConstValue> memo;
    std::unordered_set<const Symbol *> visiting;
    std::unordered_set<const Symbol *> failed;
};
