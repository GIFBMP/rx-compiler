#pragma once
#include "../AST/AstNode.h"
#include "../AST/expr.h"
#include "../AST/item.h"
#include "../AST/stmt.h"
#include "../AST/type.h"
#include "constEval.h"
#include "scope.h"
#include "semtype.h"
#include "symbol.h"
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct Diagnostic {
    SourceSpan span;
    std::string message;
};

struct FuncSig {
    std::vector<SemTypePtr> params;
    SemTypePtr ret;
    bool hasSelf = false;
    bool selfByRef = false;
    bool selfMut = false;
};

struct StructInfo {
    const StructDefItem *def = nullptr;

    struct Field {
        std::string name;
        SemTypePtr type;
        SourceSpan span{};
    };

    std::vector<Field> fields;
    std::unordered_map<std::string, Symbol *> members;
    std::vector<Derive> derives;
};

class SemanticAnalyzer {
public:
    bool analyze(Crate *crate);
    bool hasError() const;
    const std::vector<Diagnostic> &diagnostics() const;

    Symbol *lookupType(const std::string &name) const;
    Symbol *lookupValue(const std::string &name) const;
    const StructInfo *structInfo(const StructDefItem *def) const;
    const FuncSig *funcSig(const Symbol *sym) const;

    const std::unordered_map<std::string, Symbol *> &typeTable() const;
    const std::unordered_map<std::string, Symbol *> &globalTable() const;

    friend class ConstEvaluator;

private:
    void initBuiltins();
    void collectItems(Crate *crate);
    void collectStructNames(Crate *crate);
    void collectStructBody(StructDefItem *item);
    void collectFunc(FuncDefItem *item);
    void collectFuncInImpl(FuncDefItem *item, StructInfo &info);
    void collectConst(ConstItem *item);
    void collectConstInImpl(ConstItem *item, StructInfo &info);
    void collectImpl(ImplItem *item);

    const Symbol *resolveConstPath(const PathExpr *path, bool report = true);
    void evaluateConst(Symbol *sym);
    void checkConstValueType(const Symbol *sym, const ConstValue &value);
    bool constExprIsUsize(const Expr *expr);

    void checkLayoutCycles();
    bool layoutDfs(const StructDefItem *def,
                   std::unordered_map<const StructDefItem *, int> &color);
    void collectInlineStructs(const SemTypePtr &type, std::vector<const StructDefItem *> &out);

    void checkDerives();
    bool structHasDerive(const StructDefItem *def, Derive d) const;
    bool typeSupportsDerive(const SemTypePtr &type, Derive d,
                            std::set<std::pair<const StructDefItem *, Derive>> &inprogress);

    void checkEntry();

    void checkBody(Crate *crate);

    SemTypePtr checkType(Type *type);
    SemTypePtr checkPathType(PathType *type);

    void addType(const std::string &name, const SemTypePtr &type);
    void addFn(const std::string &name, const std::vector<SemTypePtr> &params, const SemTypePtr &ret);

    Symbol *declareType(std::unique_ptr<Symbol> sym);
    Symbol *declareValue(std::unique_ptr<Symbol> sym);
    Symbol *declareMember(StructInfo &info, std::unique_ptr<Symbol> sym);
    void error(SourceSpan span, std::string message);

    void checkFunc(FuncDefItem *item);
    void checkStmt(Stmt *stmt, Scope &scope);
    void checkImpl(ImplItem *item);
    SemTypePtr checkExpr(Expr *expr, Scope &scope, const SemTypePtr &expected = nullptr);

    std::vector<Diagnostic> diags;

    std::unordered_map<std::string, Symbol *> types;
    std::unordered_map<std::string, Symbol *> globals;
    std::unordered_map<const StructDefItem *, StructInfo> structs;
    std::unordered_map<const ImplItem *, const StructDefItem *> implTargets;
    std::unordered_map<const Symbol *, ConstValue> constValues;
    std::unordered_map<const Symbol *, FuncSig> sigs;
    std::vector<std::unique_ptr<Symbol>> ownedSymbols;

    ConstEvaluator constEval;
    const StructDefItem *currentImplSelf = nullptr;
};
