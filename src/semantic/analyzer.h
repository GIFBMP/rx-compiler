#pragma once
#include "../AST/AstNode.h"
#include "../AST/expr.h"
#include "../AST/item.h"
#include "../AST/stmt.h"
#include "../AST/type.h"
#include "constEval.h"
#include "scope.h"
#include "sema_result.h"
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

struct PlaceInfo {
    bool isPlace = false;
    bool isMut = false;//meaningful only when isPlace
    bool gated = false;//reached through a shared reference or an immutably-accessed Vec index
};

enum class ContainerArg { T, Usize };

enum class ContainerRet { T, BoxT, VecT, Usize, Bool, Unit };

struct ContainerMember {
    bool hasSelf = false;
    bool selfByRef = false;
    bool selfMut = false;
    std::vector<ContainerArg> params;
    ContainerRet ret = ContainerRet::Unit;
    BuiltinOp op = BuiltinOp::None;
};

struct LoopCtx {
    SemTypePtr expected;
    std::vector<SemTypePtr> breaks;
    bool isWhile = false;
    bool whileBarrier = false;
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
    const SemaResult &semaResult() const;

    friend class ConstEvaluator;

private:
    void initBuiltins();
    void initContainerMembers();
    void collectItems(Crate *crate);
    void collectStructNames(Crate *crate);
    void collectStructBody(StructDefItem *item);
    void collectFunc(FuncDefItem *item);
    void collectFuncInImpl(FuncDefItem *item, StructInfo &info);
    void collectConst(ConstItem *item);
    void collectConstInImpl(ConstItem *item, StructInfo &info);
    void collectImpl(ImplItem *item);
    void collectGeneratedClones();

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
                            std::set<std::pair<const StructDefItem *, Derive>> &vis);

    void checkEntry();

    void checkBody(Crate *crate);

    SemTypePtr checkType(Type *type);
    SemTypePtr checkPathType(PathType *type);

    void addType(const std::string &name, const SemTypePtr &type);
    void addFn(const std::string &name, const std::vector<SemTypePtr> &params, const SemTypePtr &ret,
               BuiltinOp op = BuiltinOp::None);

    Symbol *declareType(std::unique_ptr<Symbol> sym);
    Symbol *declareValue(std::unique_ptr<Symbol> sym);
    Symbol *declareMember(StructInfo &info, std::unique_ptr<Symbol> sym);
    void error(SourceSpan span, std::string message);

    void checkFunc(FuncDefItem *item);
    SemTypePtr checkStmt(Stmt *stmt, Scope &scope);
    void checkImpl(ImplItem *item);
    SemTypePtr checkExpr(Expr *expr, Scope &scope, const SemTypePtr &expected = nullptr);
    SemTypePtr checkExprNode(Expr *expr, Scope &scope, const SemTypePtr &expected);
    PlaceInfo placeInfo(Expr *expr, Scope &scope);
    SemTypePtr placeTypeOf(Expr *expr, Scope &scope);

    Symbol *resolvePathSymbol(Expr *expr, Scope &scope);
    Symbol *getContainerMember(const std::string &container, const std::string &name,
                                       const ContainerMember &member, const SemTypePtr &T,
                                       SourceSpan span);
    Symbol *getStructClone(const StructDefItem *def, SourceSpan span);
    Symbol *getBuiltinClone(const SemTypePtr &type, SourceSpan span);
    Symbol *getArrayLen(const SemTypePtr &type, SourceSpan span);
    Symbol *findMethod(const SemTypePtr &candidate, const std::string &name, SourceSpan span);
    SemTypePtr checkCallArgs(const FuncSig *sig, std::size_t base,
                             const std::vector<NodePtr<Expr>> &args, Scope &scope, SourceSpan span);
    void checkReborrow(Expr *expr, const SemTypePtr &type, const SemTypePtr &expected, Scope &scope);

    SemTypePtr checkIntExpr(IntExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkBoolExpr(BoolExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkUnitExpr(UnitExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkGroupExpr(GroupExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkUnaryExpr(UnaryExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkBinaryExpr(BinaryExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkAssignExpr(AssignExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkCastExpr(CastExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkPathExpr(PathExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkFieldExpr(FieldExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkIndexExpr(IndexExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkCallExpr(CallExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkMethodCallExpr(MethodCallExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkStructExpr(StructExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkArrayExpr(ArrayExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkIfExpr(IfExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkWhileExpr(WhileExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkLoopExpr(LoopExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkBlockExpr(BlockExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkReturnExpr(ReturnExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkBreakExpr(BreakExpr *expr, Scope &scope, const SemTypePtr &expected);
    SemTypePtr checkContinueExpr(ContinueExpr *expr, Scope &scope, const SemTypePtr &expected);

    TypeLayout layoutOf(const SemTypePtr &type) const;

    std::vector<Diagnostic> diags;
    std::vector<LoopCtx> loopStack;

    std::unordered_map<std::string, Symbol *> types;
    std::unordered_map<std::string, Symbol *> globals;
    std::unordered_map<const ImplItem *, const StructDefItem *> implTargets;
    std::unordered_map<std::string, std::unordered_map<std::string, ContainerMember>> containerMembers;

    SemaResult result;

    ConstEvaluator constEval;
    const StructDefItem *currentImplSelf = nullptr;
    SemTypePtr currentFuncRet;
};
