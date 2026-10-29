#pragma once
#include "IR.h"
#include "../semantic/analyzer.h"
#include <string>
#include <unordered_map>
#include <vector>
namespace rx::ir {
using namespace rx::ast;
using namespace rx::semantic;

struct LoopCtx {
    size_t breakBlock = 0;
    size_t continueBlock = 0;
    bool isWhile = false;
    bool hasResult = false;
    IRValue resultSlot;
    SemTypePtr resultType;
};

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

    struct FuncCtx {
        IRFunc *func = nullptr;
        size_t nxReg = 0;
        size_t curBlock = 0;
        bool curFuncIsMain = false;
        SemTypePtr curFuncRet = nullptr;
        bool curAggRet = false;
        IRValue curSret;
        std::unordered_map<const Symbol*, IRValue> locals;
    };

    IRGlobal *IntFmt = nullptr;//"%d\0"
    IRGlobal *IntlnFmt = nullptr;//"%d\n\0"

    std::unordered_map<const Symbol*, IRValue> locals;//local symbols/params;
    std::unordered_map<const StructDefItem*, IRStruct*> structs;
    std::unordered_map<const AstNode*, const Symbol*> declSyms;
    std::unordered_map<const Symbol*, const StructDefItem*> memberOwners;
    std::unordered_map<std::string, std::string> eqHelperNames;
    std::vector<SemTypePtr> pendingEqTypes;
    size_t eqHelperCounter = 0;
    std::vector<FuncCtx> ctxStack;
    std::vector<LoopCtx> loopStack;

    IRType *irType(const SemTypePtr &t);
    IRStruct *irStruct(const StructDefItem *def);
    IRValue newReg(IRType *t);
    IRValue makeIRInt(long long value, IRType *type = nullptr);
    size_t newBlock(const std::string &name);
    void switchBlock(size_t block);
    void emit(const IRInstr &instr);
    void emitUnreachable();
    void emitRet();
    void emitRet(IRValue value);
    void emitSretReturn(IRValue sret, IRValue value, IRType *type);
    IRValue emitAlloca(IRType *type);
    IRValue emitLoad(IRType *type, IRValue ptr);
    void emitStore(IRValue value, IRValue ptr, IRType *type);
    IRValue emitGep(IRType *baseType, IRValue ptr,
                    const std::vector<IRValue> &indices);
    IRValue emitZext(IRValue value, IRType *type);
    IRValue emitBinaryOp(IROp op, IRValue lhs, IRValue rhs);
    IRValue emitICmp(IRCmp cmp, IRValue lhs, IRValue rhs);
    void emitBr(size_t target);
    void emitCondBr(IRValue cond, size_t thenBlock, size_t elseBlock);
    void emitMemcpy(IRValue dst, IRValue src, size_t size);
    void emitMemcpy(IRValue dst, IRValue src, IRValue size);
    void emitMemmove(IRValue dst, IRValue src, IRValue size);
    IRValue emitCall(const std::string &calleeName, IRType *retType,
                     const std::vector<IRValue> &args, bool varArg = false);
    IRValue emitCompoundAssign(AssignOp op, IRValue lhs, IRValue rhs, SemTypePtr type);

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
    IRValue lowerPlace(Expr *expr);
    IRValue lowerRef(Expr *expr);
    IRValue lowerUnaryExpr(UnaryExpr *expr, const SemTypePtr &expected = nullptr);
    IRValue lowerBinaryOperand(Expr *expr);
    IRValue lowerArithmeticOrShift(BinaryExpr *expr);
    IRValue lowerEquality(BinaryExpr *expr);
    IRValue lowerOrdering(BinaryExpr *expr);
    IRValue lowerLogic(BinaryExpr *expr);
    IRValue lowerEqAddress(Expr *expr, SemTypePtr type);
    std::string typeKey(const SemTypePtr &type);
    std::string ensureEqHelper(const SemTypePtr &type);
    IRValue emitEqCall(const SemTypePtr &type, IRValue lhsAddr, IRValue rhsAddr);
    IRValue emitEqValue(const SemTypePtr &type, IRValue lhsAddr, IRValue rhsAddr);
    void pushFuncCtx();
    void popFuncCtx();
    void generateEqHelpers();
    void generateStructEqHelper(const SemTypePtr &type);
    void generateArrayEqHelper(const SemTypePtr &type);
    void generateVecEqHelper(const SemTypePtr &type);
    void generateRefEqHelper(const SemTypePtr &type);
    void generateBoxEqHelper(const SemTypePtr &type);
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
} // namespace rx::ir
