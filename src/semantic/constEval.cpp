#include "constEval.h"
#include "analyzer.h"

std::optional<ConstValue> ConstEvaluator::eval(const Expr *expr) {
    (void)expr;
    return std::nullopt;
}

std::optional<ConstValue> ConstEvaluator::evalConst(const Symbol *sym) {
    (void)sym;
    return std::nullopt;
}
