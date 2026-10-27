#pragma once
#include "IR.h"
#include "../semantic/analyzer.h"
#include <string>
#include <unordered_map>

class Lower {
public:
    Lower(const SemaResult &sema, TypeContext &types) : sema(sema), types(types) {}
    IRModule lower(Crate *crate);
private:
    const SemaResult &sema;
    TypeContext &types;
    IRModule module;

    IRFunc *curFunc = nullptr;
    size_t nxReg = 0;//next available reg
    size_t curBlock = 0;//cur IR block

    bool curFuncIsMain = false;
    SemTypePtr curFuncRet = nullptr;

    std::unordered_map<const Symbol*, IRValue> locals;//local symbols/params;
    std::unordered_map<const StructDefItem*, IRStruct*> structs;
    std::unordered_map<const AstNode*, const Symbol*> declSyms;
    std::unordered_map<const Symbol*, const StructDefItem*> memberOwners;

    IRType *irType(const SemTypePtr &t);
    IRStruct *irStruct(const StructDefItem *def);
    IRValue newReg(IRType *t);
    size_t newBlock(const std::string &name);
    void emit(const IRInstr &instr);

    bool terminated() const;
    const Symbol *declSymbol(const AstNode *node) const;
    const StructDefItem *memberOwner(const Symbol *sym) const;

    void lowerFunc(FuncDefItem *item);

    void preDeclareLocals(BlockExpr *body);
    void preDeclareBlock(BlockExpr *block);
    void preDeclareStmt(Stmt *stmt);
    void preDeclareExpr(Expr *expr);
    void preDeclareLet(LetStmt *stmt);

    bool lowerStmt(Stmt *stmt);//true: terminate block, false: not terminate

    bool lowerLetStmt(LetStmt *stmt);
    bool lowerExprStmt(ExprStmt *stmt);

    IRValue lowerExpr(Expr *expr, const SemTypePtr &expected = nullptr);

    IRValue lowerBoolExpr(BoolExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerIntExpr(IntExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerBlockExpr(BlockExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerPathExpr(PathExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerUnaryExpr(UnaryExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerBinaryExpr(BinaryExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerAssignExpr(AssignExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerFieldExpr(FieldExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerReturnExpr(ReturnExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerBreakExpr(BreakExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerContinueExpr(ContinueExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerIfExpr(IfExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerWhileExpr(WhileExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerLoopExpr(LoopExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerStructExpr(StructExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerArrayExpr(ArrayExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerIndexExpr(IndexExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerCallExpr(CallExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerMethodCallExpr(MethodCallExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerCastExpr(CastExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerUnitExpr(UnitExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerGroupExpr(GroupExpr *expr, const SemTypePtr &expected = nullptr);

    IRValue coerce(IRValue val, const SemTypePtr &src, const SemTypePtr &dst);
};