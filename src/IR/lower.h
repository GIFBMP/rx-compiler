#pragma once
#include "IR.h"
#include "../semantic/analyzer.h"
#include <string>
#include <unordered_map>
#include <vector>

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
    bool curAggRet = false;
    IRValue curSret;

    IRGlobal *IntFmt = nullptr;//"%d\0"
    IRGlobal *IntlnFmt = nullptr;//"%d\n\0"

    std::unordered_map<const Symbol*, IRValue> locals;//local symbols/params;
    std::unordered_map<const StructDefItem*, IRStruct*> structs;
    std::unordered_map<const AstNode*, const Symbol*> declSyms;
    std::unordered_map<const Symbol*, const StructDefItem*> memberOwners;

    IRType *irType(const SemTypePtr &t);
    IRStruct *irStruct(const StructDefItem *def);
    IRValue newReg(IRType *t);
    size_t newBlock(const std::string &name);
    void emit(const IRInstr &instr);
    void emitUnreachable();
    void emitRet();
    void emitRet(IRValue value);
    void emitSretReturn(IRValue sret, IRValue value, IRType *type);
    IRValue emitAlloca(IRType *type);
    IRValue emitLoad(IRType *type, IRValue ptr);
    void emitStore(IRValue value, IRValue ptr, IRType *type);
    void emitMemcpy(IRValue dst, IRValue src, size_t size);
    void emitMemcpy(IRValue dst, IRValue src, IRValue size);
    void emitMemmove(IRValue dst, IRValue src, IRValue size);
    IRValue emitCall(const std::string &calleeName, IRType *retType,
                     const std::vector<IRValue> &args, bool varArg = false);

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

    IRValue lowerBuiltinIO(CallExpr *expr, Symbol *callee, const FuncSig &sig, BuiltinOp op);
    IRValue lowerBuiltinFunc(CallExpr *expr, Symbol *callee, const FuncSig &sig, BuiltinOp op);
    IRValue lowerFuncCall(CallExpr *expr, Symbol *callee, const FuncSig &sig);

    IRValue coerce(IRValue val, const SemTypePtr &src, const SemTypePtr &dst);

    IRGlobal *getFmtGlobal(const std::string &str, const std::string &name);
};