#pragma once
#include "../AST/AstBuilder.h"
#include "semtype.h"
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

    SemTypePtr type;//type of the initializer expr; null = unknown/infer (adopts declared/expected)

    static ConstValue makeIntValue(const IntValue &v) {
        ConstValue c;
        c.kind = ConstKind::Int;
        c.val = v;
        switch(v.suffix) {
            case IntSuffix::I32:   c.type = SemType::make(SemKind::I32);   break;
            case IntSuffix::U32:   c.type = SemType::make(SemKind::U32);   break;
            case IntSuffix::Isize: c.type = SemType::make(SemKind::Isize); break;
            case IntSuffix::Usize: c.type = SemType::make(SemKind::Usize); break;
            case IntSuffix::Infer: c.type = nullptr;                       break;
        }
        return c;
    }

    static ConstValue makeBoolValue(bool b) {
        ConstValue c;
        c.kind = ConstKind::Bool;
        c.boolVal = b;
        c.type = SemType::make(SemKind::Bool);
        return c;
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
