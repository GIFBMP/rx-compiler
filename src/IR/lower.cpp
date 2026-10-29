#include "lower.h"
#include "semantic/symbol.h"

#include <sstream>
#include <utility>
namespace rx::ir {
using namespace rx::ast;
using namespace rx::semantic;

static bool isAggregateType(const IRType *type) {
    return type != nullptr &&
           (type->kind == IRKind::Struct || type->kind == IRKind::Array);
}

static bool isICmpOperandType(const IRType *type) {
    return type != nullptr &&
           (type->kind == IRKind::I1 ||
            type->kind == IRKind::I8 ||
            type->kind == IRKind::I32 ||
            type->kind == IRKind::Ptr);
}

IRModule Lower::lower(Crate *crate) {
    eqHelperNames.clear();
    pendingEqTypes.clear();
    eqHelperCounter = 0;
    ctxStack.clear();
    loopStack.clear();

    IntFmt = getFmtGlobal("%d", ".fmt_int");
    IntlnFmt = getFmtGlobal("%d\n", ".fmt_int_nl");

    declSyms.clear();
    for(auto &it: sema.sigs) {
        const auto *sym = it.first;
        const auto *decl = sym->decl;
        if(decl == nullptr || decl->typ != NodeType::FuncDefItem) continue;//builtin/Box/Vec/clone runtime
        declSyms[sym->decl] = sym;
    }//collect funcs

    memberOwners.clear();
    for(auto &it: sema.structs) {
        const auto *def = it.first;
        const auto &info = it.second;
        for(const auto &mem: info.members) {
            memberOwners[mem.second] = def;
        }
    }//collect structs

    for(auto &it: crate->items) {
        switch(it->typ) {
            case NodeType::FuncDefItem: {
                lowerFunc(static_cast<FuncDefItem*>(it.get()));
                break;
            }
            case NodeType::ImplItem: {
                auto *impl = static_cast<ImplItem*>(it.get());
                for(auto &mem: impl->items) {
                    if(mem->typ == NodeType::FuncDefItem) {
                        lowerFunc(static_cast<FuncDefItem*>(mem.get()));
                    }
                }
                break;
            }
            default: break;
        }
    }

    generateEqHelpers();
    return std::move(module);
}

IRType *Lower::irType(const SemTypePtr &t) {
    if(!t) return types.voidType();
    switch(t->kind) {
        case SemKind::I32:
        case SemKind::U32:
        case SemKind::Isize:
        case SemKind::Usize: {
            return types.i32();
        }
        case SemKind::Bool: {
            return types.i1();
        }
        case SemKind::Array: {
            return types.array(irType(t->elem), t->arrayLen);
        }
        case SemKind::Ref:
        case SemKind::Box: {//elem type does not make sense to align and size(llvm uses opaque pointer)
            return types.ptr();
        }
        case SemKind::Vec: {
            return types.vecHeader();
        }
        case SemKind::Struct: {
            return types.structType(irStruct(t->structDef));
        }
        case SemKind::Never:
        case SemKind::Unit: {
            return types.voidType();
        }
        default: {
            return types.voidType();
        }
    }
}

IRStruct *Lower::irStruct(const StructDefItem *def) {
    if(def == nullptr) {
        return nullptr;
    }
    auto cache = structs.find(def);
    if(cache != structs.end()) {
        return cache->second;
    }
    auto it = sema.structs.find(def);
    if(it == sema.structs.end()) {
        return nullptr;
    }
    const auto &info = it->second;
    std::vector<IRField> fields;
    fields.reserve(info.fields.size());
    for(const auto &field: info.fields) {
        fields.push_back(IRField{field.name, irType(field.type), 0});
    }
    IRStruct *ret = types.makeStruct(def->name, fields, info.fieldOffsets,
                                     info.layout.size, info.layout.align);
    structs[def] = ret;
    return ret;
}

IRValue Lower::newReg(IRType *t) {
    IRValue ret;
    ret.kind = IRValueKind::Reg;
    ret.id = nxReg;
    nxReg++;
    ret.type = t;
    return ret;
}

IRValue Lower::makeIRInt(long long value, IRType *type) {
    if(type == nullptr) {
        type = types.i32();
    }
    assert(type != nullptr);

    IRValue ret;
    ret.kind = IRValueKind::Int;
    ret.type = type;
    ret.intValue = value;
    return ret;
}

size_t Lower::newBlock(const std::string &name) {
    assert(curFunc != nullptr);
    size_t id = curFunc->blocks.size();
    auto block = std::make_unique<IRBlock>();
    block->name = name;
    curFunc->blocks.push_back(std::move(block));
    return id;
}

void Lower::switchBlock(size_t block) {
    assert(curFunc != nullptr);
    assert(block < curFunc->blocks.size());
    curBlock = block;
}

void Lower::emit(const IRInstr &instr) {
    assert(curFunc != nullptr);
    assert(curBlock < curFunc->blocks.size());
    assert(!terminated());
    curFunc->blocks[curBlock]->instrs.push_back(std::make_unique<IRInstr>(instr));
}

void Lower::emitBr(size_t target) {
    IRInstr instr;
    instr.op = IROp::Br;
    instr.thenBrBlock = target;
    emit(instr);
}

void Lower::emitCondBr(IRValue cond, size_t thenBlock, size_t elseBlock) {
    IRInstr instr;
    instr.op = IROp::CondBr;
    instr.type = types.i1();
    instr.args.push_back(cond);
    instr.thenBrBlock = thenBlock;
    instr.elseBrBlock = elseBlock;
    emit(instr);
}

void Lower::emitUnreachable() {
    IRInstr unr;
    unr.op = IROp::Unreachable;
    emit(unr);
}

void Lower::emitRet() {
    IRInstr instr;
    instr.op = IROp::Ret;
    instr.type = types.voidType();
    emit(instr);
}

void Lower::emitRet(IRValue value) {
    assert(value.type != nullptr);
    assert(!isAggregateType(value.type));

    IRInstr instr;
    instr.op = IROp::Ret;
    instr.type = value.type;
    instr.args.push_back(value);
    emit(instr);
}

void Lower::emitSretReturn(IRValue sret, IRValue value, IRType *type) {
    assert(type != nullptr);
    assert(isAggregateType(type));

    emitMemcpy(sret, value, type->size);
    emitRet();
}

IRValue Lower::emitAlloca(IRType *type) {
    assert(type != nullptr);
    assert(curFunc != nullptr);
    assert(!curFunc->blocks.empty());
    assert(type->kind != IRKind::Void);

    IRValue slot = newReg(types.ptr());
    IRInstr instr;
    instr.op = IROp::Alloca;
    instr.type = type;
    instr.dst = slot;
    instr.hasDst = true;
    instr.align = type->align;

    auto &entry = curFunc->blocks.front()->instrs;
    size_t pos = entry.size();
    if(pos != 0) {
        switch(entry.back()->op) {
            case IROp::Ret:
            case IROp::Br:
            case IROp::CondBr:
            case IROp::Unreachable: {
                pos--;
                break;
            }
            default: break;
        }
    }
    entry.insert(entry.begin() + pos, std::make_unique<IRInstr>(instr));
    return slot;
}

IRValue Lower::emitLoad(IRType *type, IRValue ptr) {
    assert(type != nullptr);
    IRValue dst = newReg(type);
    IRInstr instr;
    instr.op = IROp::Load;
    instr.type = type;
    instr.dst = dst;
    instr.hasDst = true;
    instr.args.push_back(ptr);
    emit(instr);
    return dst;
}

void Lower::emitStore(IRValue value, IRValue ptr, IRType *type) {
    assert(type != nullptr);
    IRInstr instr;
    instr.op = IROp::Store;
    instr.type = type;
    instr.args.push_back(value);
    instr.args.push_back(ptr);
    emit(instr);
}

IRValue Lower::emitGep(IRType *baseType, IRValue ptr,
                       const std::vector<IRValue> &indices) {
    assert(baseType != nullptr);
    assert(ptr.type != nullptr);

    IRValue dst = newReg(types.ptr());
    IRInstr instr;
    instr.op = IROp::Gep;
    instr.type = baseType;
    instr.dst = dst;
    instr.hasDst = true;
    instr.args.push_back(ptr);

    for(const auto &index : indices) {
        assert(index.type != nullptr);
        assert(index.type->kind == IRKind::I8 ||
               index.type->kind == IRKind::I32);
        instr.args.push_back(index);
    }

    emit(instr);
    return dst;
}

IRValue Lower::emitICmp(IRCmp cmp, IRValue lhs, IRValue rhs) {
    assert(lhs.type != nullptr);
    assert(rhs.type != nullptr);
    assert(lhs.type == rhs.type);
    assert(isICmpOperandType(lhs.type));

    IRInstr instr;
    instr.op = IROp::ICmp;
    instr.type = types.i1();
    instr.dst = newReg(types.i1());
    instr.hasDst = true;
    instr.cmp = cmp;
    instr.args.push_back(lhs);
    instr.args.push_back(rhs);
    emit(instr);
    return instr.dst;
}

IRValue Lower::emitZext(IRValue value, IRType *type) {
    assert(value.type != nullptr);
    assert(type != nullptr);
    assert(value.type != type);
    assert(value.type->kind == IRKind::I1 ||
           value.type->kind == IRKind::I8 ||
           value.type->kind == IRKind::I32);
    assert(type->kind == IRKind::I8 ||
           type->kind == IRKind::I32);

    IRInstr instr;
    instr.op = IROp::Zext;
    instr.type = type;
    instr.dst = newReg(type);
    instr.hasDst = true;
    instr.args.push_back(value);
    emit(instr);
    return instr.dst;
}

IRValue Lower::emitBinaryOp(IROp op, IRValue lhs, IRValue rhs) {
    switch(op) {
        case IROp::Add:
        case IROp::Sub:
        case IROp::Mul:
        case IROp::SDiv:
        case IROp::UDiv:
        case IROp::SRem:
        case IROp::URem:
        case IROp::And:
        case IROp::Or:
        case IROp::Xor:
        case IROp::Shl:
        case IROp::LShr:
        case IROp::AShr: {
            break;
        }
        default: {
            assert(false);
            return {};
        }
    }

    assert(lhs.type != nullptr);
    assert(rhs.type != nullptr);
    assert(lhs.type == rhs.type);

    IRInstr instr;
    instr.op = op;
    instr.type = lhs.type;
    instr.dst = newReg(lhs.type);
    instr.hasDst = true;
    instr.args.push_back(lhs);
    instr.args.push_back(rhs);
    emit(instr);
    return instr.dst;
}

void Lower::emitMemcpy(IRValue dst, IRValue src, IRValue size) {
    IRInstr copy;
    copy.op = IROp::Memcpy;
    copy.args.push_back(dst);
    copy.args.push_back(src);
    copy.args.push_back(size);
    emit(copy);
}

void Lower::emitMemcpy(IRValue dst, IRValue src, size_t size) {
    emitMemcpy(dst, src, makeIRInt(static_cast<long long>(size)));
}

void Lower::emitMemmove(IRValue dst, IRValue src, IRValue size) {
    IRInstr move;
    move.op = IROp::Memmove;
    move.args.push_back(dst);
    move.args.push_back(src);
    move.args.push_back(size);
    emit(move);
}

bool Lower::terminated() const {
    if(curFunc == nullptr || curBlock >= curFunc->blocks.size()) {
        return false;
    }//invalid
    const auto &instrs = curFunc->blocks[curBlock]->instrs;
    if(instrs.empty()) return false;
    switch(instrs.back()->op) {
        case IROp::Ret:
        case IROp::Br:
        case IROp::CondBr:
        case IROp::Unreachable: {
            return true;
        }
        default: {
            return false;
        }
    }
}

const Symbol *Lower::declSymbol(const AstNode *node) const {
    auto it = declSyms.find(node);
    return it == declSyms.end() ? nullptr : it->second;
}

const StructDefItem *Lower::memberOwner(const Symbol *sym) const {
    auto it = memberOwners.find(sym);
    return it == memberOwners.end() ? nullptr : it->second;
}

void Lower::preDeclareLocals(BlockExpr *body) {
    if(body == nullptr) return;
    assert(curFunc != nullptr);
    assert(!curFunc->blocks.empty());

    size_t saved = curBlock;
    curBlock = 0;
    preDeclareBlock(body);
    curBlock = saved;
}

void Lower::preDeclareBlock(BlockExpr *block) {
    if(block == nullptr) return;
    for(auto &stmt: block->statements) {
        preDeclareStmt(stmt.get());
    }
    preDeclareExpr(block->tail.get());
}

void Lower::preDeclareStmt(Stmt *stmt) {
    if(stmt == nullptr) return;
    switch(stmt->typ) {
        case NodeType::LetStmt: {
            auto *let = static_cast<LetStmt*>(stmt);
            preDeclareExpr(let->init.get());
            preDeclareLet(let);
            break;
        }
        case NodeType::ExprStmt: {
            preDeclareExpr(static_cast<ExprStmt*>(stmt)->expr.get());
            break;
        }
        case NodeType::NullStmt:
        default: {
            break;
        }
    }
}

void Lower::preDeclareExpr(Expr *expr) {
    if(expr == nullptr) return;
    switch(expr->typ) {
        case NodeType::BlockExpr: {
            preDeclareBlock(static_cast<BlockExpr*>(expr));
            break;
        }
        case NodeType::IfExpr: {
            auto *e = static_cast<IfExpr*>(expr);
            preDeclareExpr(e->cond.get());
            preDeclareBlock(e->thenBlock.get());
            preDeclareExpr(e->elseBranch.get());
            break;
        }
        case NodeType::WhileExpr: {
            auto *e = static_cast<WhileExpr*>(expr);
            preDeclareExpr(e->cond.get());
            preDeclareBlock(e->body.get());
            break;
        }
        case NodeType::LoopExpr: {
            preDeclareBlock(static_cast<LoopExpr*>(expr)->body.get());
            break;
        }
        case NodeType::UnaryExpr: {
            preDeclareExpr(static_cast<UnaryExpr*>(expr)->num.get());
            break;
        }
        case NodeType::BinaryExpr: {
            auto *e = static_cast<BinaryExpr*>(expr);
            preDeclareExpr(e->lhs.get());
            preDeclareExpr(e->rhs.get());
            break;
        }
        case NodeType::AssignExpr: {
            auto *e = static_cast<AssignExpr*>(expr);
            preDeclareExpr(e->lhs.get());
            preDeclareExpr(e->rhs.get());
            break;
        }
        case NodeType::FieldExpr: {
            preDeclareExpr(static_cast<FieldExpr*>(expr)->base.get());
            break;
        }
        case NodeType::IndexExpr: {
            auto *e = static_cast<IndexExpr*>(expr);
            preDeclareExpr(e->base.get());
            preDeclareExpr(e->index.get());
            break;
        }
        case NodeType::CallExpr: {
            auto *e = static_cast<CallExpr*>(expr);
            preDeclareExpr(e->callee.get());
            for(auto &arg: e->args) {
                preDeclareExpr(arg.get());
            }
            break;
        }
        case NodeType::MethodCallExpr: {
            auto *e = static_cast<MethodCallExpr*>(expr);
            preDeclareExpr(e->receiver.get());
            for(auto &arg: e->args) {
                preDeclareExpr(arg.get());
            }
            break;
        }
        case NodeType::StructExpr: {
            auto *e = static_cast<StructExpr*>(expr);
            preDeclareExpr(e->path.get());
            for(auto &field: e->fields) {
                preDeclareExpr(field->value.get());
            }
            break;
        }
        case NodeType::ArrayExpr: {
            auto *e = static_cast<ArrayExpr*>(expr);
            if(e->isRepeat) {
                preDeclareExpr(e->repeatValue.get());
                preDeclareExpr(e->repeatLen.get());
            }
            else {
                for(auto &elem: e->elems) {
                    preDeclareExpr(elem.get());
                }
            }
            break;
        }
        case NodeType::CastExpr: {
            preDeclareExpr(static_cast<CastExpr*>(expr)->expr.get());
            break;
        }
        case NodeType::GroupExpr: {
            preDeclareExpr(static_cast<GroupExpr*>(expr)->inner.get());
            break;
        }
        case NodeType::ReturnExpr: {
            preDeclareExpr(static_cast<ReturnExpr*>(expr)->val.get());
            break;
        }
        case NodeType::BreakExpr: {
            preDeclareExpr(static_cast<BreakExpr*>(expr)->val.get());
            break;
        }
        case NodeType::ContinueExpr:
        case NodeType::PathExpr:
        case NodeType::IntExpr:
        case NodeType::BoolExpr:
        case NodeType::UnitExpr:
        default: {
            break;
        }
    }
}

void Lower::preDeclareLet(LetStmt *stmt) {
    if(stmt == nullptr) return;
    auto *sym = sema.symbolOf(stmt);
    assert(sym != nullptr);
    if(sym == nullptr) return;

    if(locals.find(sym) != locals.end()) return;

    IRType *ty = irType(sym->type);
    if(ty == nullptr || ty->kind == IRKind::Void) return;

    IRValue slot = emitAlloca(ty);

    locals[sym] = slot;
}

void Lower::lowerFunc(FuncDefItem *item) {
    auto *sym = declSymbol(item);
    assert(sym != nullptr);

    auto sigIt = sema.sigs.find(sym);
    assert(sigIt != sema.sigs.end());
    auto &sig = sigIt->second;

    auto *owner = memberOwner(sym);//impl case
    bool isMain = (owner == nullptr && item->name == "main");

    IRType *retTy = irType(sig.ret);
    bool aggRet = isAggregateType(retTy);

    auto func = std::make_unique<IRFunc>();
    func->name = owner ? owner->name + "." + item->name : item->name;
    func->retType = isMain ? types.i32() : (aggRet ? types.voidType() : retTy);

    module.funcs.push_back(std::move(func));
    curFunc = module.funcs.back().get();

    nxReg = 0;
    curBlock = 0;
    locals.clear();
    loopStack.clear();
    curFuncRet = sig.ret;
    curFuncIsMain = isMain;//re-initialize
    curAggRet = aggRet;
    curSret = {};
    curBlock = newBlock("entry");

    if(aggRet) {
        curSret = newReg(types.ptr());
        curFunc->params.push_back(curSret);//sret, %out+memcpy+ret void
    }

    for(auto &p: item->params) {//allocate params
        auto *paramSym = sema.symbolOf(p.get());
        assert(paramSym != nullptr);
        IRType *storageType = irType(paramSym->type);
        assert(storageType->kind != IRKind::Void);

        bool byPtr = isAggregateType(storageType);
        IRType *paramType = byPtr ? types.ptr() : storageType;

        auto reg = newReg(paramType);//imm reg, just for store
        curFunc->params.push_back(reg);

        auto stackSlot = emitAlloca(storageType);
        locals[paramSym] = stackSlot;//alloca

        if(byPtr) {
            emitMemcpy(stackSlot, reg, storageType->size);
        }
        else {
            emitStore(reg, stackSlot, storageType);//store
        }
    }

    //preprocess the letstmt(entry alloca, according to standard llvm)
    preDeclareLocals(item->body.get());

    auto tail = lowerBlockExpr(item->body.get(), sig.ret);
    if(terminated()) return;

    if(item->body) {
        SemTypePtr bodyType = sema.typeOf(item->body.get());
        if(bodyType && bodyType->isNever()) {
            emitUnreachable();
            return;
        }
    }

    if(isMain) {//ret i32 0
        emitRet(makeIRInt(0, types.i32()));
    }
    else if(aggRet) {
        emitSretReturn(curSret, tail, retTy);
    }
    else if(!sig.ret || sig.ret->kind == SemKind::Unit) {
        emitRet();
    }
    else {
        IRValue val = coerce(tail, item->body->tail ? sema.typeOf(item->body->tail.get()) : nullptr, sig.ret);
        emitRet(val);
    }
}

bool Lower::lowerLetStmt(LetStmt *stmt) {
    auto *sym = sema.symbolOf(stmt);
    if(sym == nullptr) return terminated();
    
    auto *typ = irType(sym->type);
    auto it = locals.find(sym);
    
    
    auto val = lowerExpr(stmt->init.get(), sym->type);
    if(terminated()) return true;
    if(typ->kind == IRKind::Void) return false;
    if(it == locals.end()) return terminated();

    val = coerce(val, sema.typeOf(stmt->init.get()), sym->type);
    auto slot = it->second;

    if(isAggregateType(typ)) {
        emitMemcpy(slot, val, typ->size);
    }
    else {
        emitStore(val, slot, typ);
    }
    return terminated();
}

bool Lower::lowerExprStmt(ExprStmt *stmt) {
    auto expected = stmt->hasSemi ? nullptr : SemType::makeUnit();
    auto val = lowerExpr(stmt->expr.get(), expected);
    (void)val;
    return terminated();
}

bool Lower::lowerStmt(Stmt *stmt) {
    if(stmt == nullptr) {
        return terminated();
    }
    switch(stmt->typ) {
        case NodeType::LetStmt: {
            return lowerLetStmt(static_cast<LetStmt*>(stmt));
        }
        case NodeType::ExprStmt: {
            return lowerExprStmt(static_cast<ExprStmt*>(stmt));
        }
        case NodeType::NullStmt:
        default: {
            return terminated();
        }
    }
}

IRValue Lower::lowerExpr(Expr *expr, const SemTypePtr &expected) {
    if(expr == nullptr) {
        return {};
    }
    switch(expr->typ) {
        case NodeType::IntExpr:        return lowerIntExpr(static_cast<IntExpr*>(expr), expected);
        case NodeType::BoolExpr:       return lowerBoolExpr(static_cast<BoolExpr*>(expr), expected);
        case NodeType::UnitExpr:       return lowerUnitExpr(static_cast<UnitExpr*>(expr), expected);
        case NodeType::GroupExpr:      return lowerGroupExpr(static_cast<GroupExpr*>(expr), expected);
        case NodeType::UnaryExpr:      return lowerUnaryExpr(static_cast<UnaryExpr*>(expr), expected);
        case NodeType::BinaryExpr:     return lowerBinaryExpr(static_cast<BinaryExpr*>(expr), expected);
        case NodeType::AssignExpr:     return lowerAssignExpr(static_cast<AssignExpr*>(expr), expected);
        case NodeType::CastExpr:       return lowerCastExpr(static_cast<CastExpr*>(expr), expected);
        case NodeType::PathExpr:       return lowerPathExpr(static_cast<PathExpr*>(expr), expected);
        case NodeType::FieldExpr:      return lowerFieldExpr(static_cast<FieldExpr*>(expr), expected);
        case NodeType::IndexExpr:      return lowerIndexExpr(static_cast<IndexExpr*>(expr), expected);
        case NodeType::CallExpr:       return lowerCallExpr(static_cast<CallExpr*>(expr), expected);
        case NodeType::MethodCallExpr: return lowerMethodCallExpr(static_cast<MethodCallExpr*>(expr), expected);
        case NodeType::StructExpr:     return lowerStructExpr(static_cast<StructExpr*>(expr), expected);
        case NodeType::ArrayExpr:      return lowerArrayExpr(static_cast<ArrayExpr*>(expr), expected);
        case NodeType::IfExpr:         return lowerIfExpr(static_cast<IfExpr*>(expr), expected);
        case NodeType::WhileExpr:      return lowerWhileExpr(static_cast<WhileExpr*>(expr), expected);
        case NodeType::LoopExpr:       return lowerLoopExpr(static_cast<LoopExpr*>(expr), expected);
        case NodeType::BlockExpr:      return lowerBlockExpr(static_cast<BlockExpr*>(expr), expected);
        case NodeType::ReturnExpr:     return lowerReturnExpr(static_cast<ReturnExpr*>(expr), expected);
        case NodeType::BreakExpr:      return lowerBreakExpr(static_cast<BreakExpr*>(expr), expected);
        case NodeType::ContinueExpr:   return lowerContinueExpr(static_cast<ContinueExpr*>(expr), expected);
        default: break;
    }
    return {};
}

IRValue Lower::lowerBoolExpr(BoolExpr *expr, const SemTypePtr &expected) {
    //imm number, does not need reg
    (void)expected;
    return makeIRInt(expr->val ? 1 : 0, types.i1());
}

IRValue Lower::lowerIntExpr(IntExpr *expr, const SemTypePtr &expected) {
    //imm number, does not need reg
    (void)expected;
    return makeIRInt(static_cast<long long>(expr->val.val));
}

IRValue Lower::lowerBlockExpr(BlockExpr *expr, const SemTypePtr &expected) {
    if(!expr) {
        return {};
    }
    for(auto &stmt: expr->statements) {
        if(lowerStmt(stmt.get())) {
            return {};//terminated
        }
    }
    if(expr->tail && !terminated()) {
        return lowerExpr(expr->tail.get(), expected);
    }
    return {};
}

IRValue Lower::lowerPathExpr(PathExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    auto *sym = sema.symbolOf(expr);
    if(sym == nullptr) return {};

    switch(sym->kind) {
        case SymbolKind::Local:
        case SymbolKind::Param: {
            auto it = locals.find(sym);
            if(it == locals.end()) return {};
            auto *typ = irType(sym->type);
            if(typ == nullptr || typ->kind == IRKind::Void) return {};

            if(typ->kind == IRKind::Struct || typ->kind == IRKind::Array) {
                //aggregate types
                return it->second;
            }
            else {
                return emitLoad(typ, it->second);
            }
        }
        case SymbolKind::Const:
        case SymbolKind::AssocConst: {
            auto it = sema.constValues.find(sym);
            if(it == sema.constValues.end()) return {};

            auto constVal = it->second;
            auto typ = sema.typeOf(expr);
            if(!typ) typ = sym->type;

            IRValue val;
            val.kind = IRValueKind::Int;
            val.type = irType(typ);
            if(constVal.isInt()) {
                val.intValue = static_cast<long long>(constVal.val.val);
            }
            else {
                val.intValue = constVal.boolVal ? 1 : 0;
            }
            return val;
        }
        default: {//fn: not a value
            assert(false);
            return {};
        }
    }
}

IRValue Lower::lowerPlace(Expr *expr) {
    if(expr == nullptr) return {};

    switch(expr->typ) {
        case NodeType::GroupExpr: {
            return lowerPlace(static_cast<GroupExpr*>(expr)->inner.get());
        }
        case NodeType::PathExpr: {
            auto *sym = sema.symbolOf(expr);
            if(sym == nullptr) return {};
            if(sym->kind != SymbolKind::Local && sym->kind != SymbolKind::Param) {
                return {};
            }
            auto it = locals.find(sym);
            if(it == locals.end()) return {};
            return it->second;
        }
        case NodeType::UnaryExpr: {
            auto *unary = static_cast<UnaryExpr*>(expr);
            if(unary->op != UnaryOp::Deref) return {};

            auto type = sema.typeOf(unary->num.get());
            if(!type || (type->kind != SemKind::Ref && type->kind != SemKind::Box)) {
                return {};
            }

            auto ptr = lowerExpr(unary->num.get(), nullptr);
            if(terminated() || ptr.type == nullptr) return {};
            return ptr;
        }
        case NodeType::FieldExpr: {
            return lowerFieldAddress(static_cast<FieldExpr*>(expr));
        }
        case NodeType::IndexExpr: {
            return lowerIndexAddress(static_cast<IndexExpr*>(expr));
        }
        default:
            return {};
    }
}

IRValue Lower::lowerRef(Expr *expr) {
    if(expr == nullptr) return {};

    auto addr = lowerPlace(expr);
    if(addr.type != nullptr) {
        //place: get addr directly
        return addr;
    }

    if(expr->typ == NodeType::FieldExpr || expr->typ == NodeType::IndexExpr) {
        //TODO
        return {};
    }
    //not place: alloca->store->get addr
    auto operandType = sema.typeOf(expr);
    if(!operandType || operandType->isError()) return {};

    auto *type = irType(operandType);
    if(type == nullptr || type->kind == IRKind::Void) return {};

    auto value = lowerExpr(expr, nullptr);
    if(terminated() || value.type == nullptr) return {};

    if(isAggregateType(type)) {
        return value;
    }

    auto slot = emitAlloca(type);
    emitStore(value, slot, type);
    return slot;
}

IRValue Lower::lowerUnaryExpr(UnaryExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    if(expr == nullptr || expr->num == nullptr) {
        return {};
    }
    switch(expr->op) {
        case UnaryOp::Neg: {
            auto numType = sema.typeOf(expr->num.get());
            if(!numType || numType->isError()) return {};

            auto val = lowerExpr(expr->num.get(), nullptr);
            if(terminated() || val.type == nullptr) return {};

            auto exprType = sema.typeOf(expr);
            if(!exprType || exprType->isError()) return {};

            auto typ = irType(exprType);
            if(typ == nullptr || typ->kind == IRKind::Void) {
                return {};
            }

            if(numType->kind == SemKind::Ref) {
                if(numType->isMut) return {};//invalid, already rejected in semantic
                val = emitLoad(typ, val);
            }

            return emitBinaryOp(IROp::Sub, makeIRInt(0, typ), val);
        }
        case UnaryOp::Not: {
            auto numType = sema.typeOf(expr->num.get());
            if(!numType || numType->isError()) return {};

            auto val = lowerExpr(expr->num.get(), nullptr);
            if(terminated() || val.type == nullptr) return {};

            auto exprType = sema.typeOf(expr);
            if(!exprType || exprType->isError()) return {};

            auto typ = irType(exprType);
            if(typ == nullptr || typ->kind == IRKind::Void) {
                return {};
            }

            if(numType->kind == SemKind::Ref) {
                if(numType->isMut) return {};//invalid, already rejected in semantic
                val = emitLoad(typ, val);
            }

            IRValue tmp = exprType->isBool() ? makeIRInt(1, types.i1()) : makeIRInt(-1, typ);
            return emitBinaryOp(IROp::Xor, val, tmp);
        }
        case UnaryOp::Ref: {
            return lowerRef(expr->num.get());
        }
        case UnaryOp::Deref: {
            //scalar: load
            //aggregate: ret ptr
            auto numType = sema.typeOf(expr->num.get());
            if(!numType || (numType->kind != SemKind::Ref && numType->kind != SemKind::Box)) {//invalid
                return {};
            }
            auto ptr = lowerExpr(expr->num.get(), nullptr);
            if(terminated() || ptr.type == nullptr) return {};

            auto exprType = sema.typeOf(expr);
            if(!exprType || exprType->isError()) return {};

            auto typ = irType(exprType);
            if(typ == nullptr || typ->kind == IRKind::Void) {
                return {};
            }

            if(isAggregateType(typ)) {
                return ptr;
            }
            return emitLoad(typ, ptr);
        }
        case UnaryOp::RefMut: {
            return lowerRef(expr->num.get());
        }
    }
    return {};
}

IRValue Lower::lowerBinaryOperand(Expr *expr) {
    if(expr == nullptr) return {};

    auto type = sema.typeOf(expr);
    if(!type || type->isError()) return {};

    auto val = lowerExpr(expr, nullptr);
    if(terminated() || val.type == nullptr) return {};

    if(type->kind == SemKind::Ref) {
        if(type->isMut) return {};

        auto inner = type->inner;
        if(!inner || (!inner->isInteger() && !inner->isBool())) return {};

        auto *innerType = irType(inner);
        if(innerType == nullptr || innerType->kind == IRKind::Void) return {};
        val = emitLoad(innerType, val);
    }

    return val;
}

std::string Lower::typeKey(const SemTypePtr &type) {
    if(!type) return "null";

    switch(type->kind) {
        case SemKind::I32:   return "i32";
        case SemKind::U32:   return "u32";
        case SemKind::Isize: return "isize";
        case SemKind::Usize: return "usize";
        case SemKind::Bool:  return "bool";
        case SemKind::Unit:  return "unit";
        case SemKind::Never: return "never";
        case SemKind::Error: return "error";
        case SemKind::Struct: {
            std::ostringstream os;
            os << "struct:";
            if(type->structDef != nullptr) {
                os << static_cast<const void *>(type->structDef);
            }
            else {
                os << "null";
            }
            return os.str();
        }
        case SemKind::Array:
            return "array:" + std::to_string(type->arrayLen) + ":" + typeKey(type->elem);
        case SemKind::Box:
            return "box:" + typeKey(type->inner);
        case SemKind::Vec:
            return "vec:" + typeKey(type->inner);
        case SemKind::Ref:
            return std::string("ref:") + (type->isMut ? "mut:" : "shared:") +
                   typeKey(type->inner);
    }

    return "unknown";
}

std::string Lower::ensureEqHelper(const SemTypePtr &type) {
    std::string key = typeKey(type);
    auto it = eqHelperNames.find(key);
    if(it != eqHelperNames.end()) {
        return it->second;
    }
    std::string name = "__rx_eq$" + std::to_string(eqHelperCounter);
    eqHelperCounter++;
    eqHelperNames[key] = name;
    pendingEqTypes.push_back(type);
    return name;
}

IRValue Lower::emitEqCall(const SemTypePtr &type, IRValue lhsAddr, IRValue rhsAddr) {
    std::string name = ensureEqHelper(type);
    return emitCall(name, types.i1(), {lhsAddr, rhsAddr}, false);
}

IRValue Lower::emitEqValue(const SemTypePtr &type, IRValue lhsAddr, IRValue rhsAddr) {
    if(type->isInteger() || type->isBool()) {
        auto lhs = emitLoad(irType(type), lhsAddr);
        auto rhs = emitLoad(irType(type), rhsAddr);
        return emitICmp(IRCmp::Eq, lhs, rhs);
    }
    return emitEqCall(type, lhsAddr, rhsAddr);
}

void Lower::pushFuncCtx() {
    FuncCtx ctx;
    ctx.func = curFunc;
    ctx.nxReg = nxReg;
    ctx.curBlock = curBlock;
    ctx.curFuncIsMain = curFuncIsMain;
    ctx.curFuncRet = curFuncRet;
    ctx.curAggRet = curAggRet;
    ctx.curSret = curSret;
    ctx.locals = locals;
    ctxStack.push_back(std::move(ctx));
}

void Lower::popFuncCtx() {
    assert(!ctxStack.empty());

    FuncCtx ctx = std::move(ctxStack.back());
    ctxStack.pop_back();

    curFunc = ctx.func;
    nxReg = ctx.nxReg;
    curBlock = ctx.curBlock;
    curFuncIsMain = ctx.curFuncIsMain;
    curFuncRet = ctx.curFuncRet;
    curAggRet = ctx.curAggRet;
    curSret = ctx.curSret;
    locals = std::move(ctx.locals);
}

void Lower::generateEqHelpers() {
    for(size_t i = 0; i < pendingEqTypes.size(); i++) {
        SemTypePtr type = pendingEqTypes[i];
        auto it = eqHelperNames.find(typeKey(type));
        if(it == eqHelperNames.end()) {
            continue;
        }

        pushFuncCtx();

        auto func = std::make_unique<IRFunc>();
        func->name = it->second;
        func->retType = types.i1();
        module.funcs.push_back(std::move(func));
        curFunc = module.funcs.back().get();

        nxReg = 0;
        curBlock = 0;
        locals.clear();
        curFuncIsMain = false;
        curFuncRet = nullptr;
        curAggRet = false;
        curSret = {};
        curBlock = newBlock("entry");

        IRValue lhs = newReg(types.ptr());
        IRValue rhs = newReg(types.ptr());
        curFunc->params.push_back(lhs);
        curFunc->params.push_back(rhs);

        switch(type->kind) {
            case SemKind::Struct:
                generateStructEqHelper(type);
                break;
            case SemKind::Array:
                generateArrayEqHelper(type);
                break;
            case SemKind::Vec:
                generateVecEqHelper(type);
                break;
            case SemKind::Ref:
                generateRefEqHelper(type);
                break;
            case SemKind::Box:
                generateBoxEqHelper(type);
                break;
            default:
                break;
        }

        if(!terminated()) {
            emitUnreachable();
        }
        popFuncCtx();
    }
}

void Lower::generateStructEqHelper(const SemTypePtr &type) {
    if(!type || type->kind != SemKind::Struct || type->structDef == nullptr) {
        return;
    }

    auto it = sema.structs.find(type->structDef);
    if(it == sema.structs.end()) {
        return;
    }

    IRStruct *info = irStruct(type->structDef);
    IRType *structType = irType(type);
    if(info == nullptr || structType == nullptr) {
        return;
    }

    IRValue lhs = curFunc->params[0];
    IRValue rhs = curFunc->params[1];
    IRValue zero = makeIRInt(0, types.i32());
    IRValue result = makeIRInt(1, types.i1());

    for(size_t i = 0; i < it->second.fields.size(); i++) {
        IRValue index = makeIRInt(static_cast<long long>(i), types.i32());
        std::vector<IRValue> indices{zero, index};

        IRValue lhsField = emitGep(structType, lhs, indices);
        IRValue rhsField = emitGep(structType, rhs, indices);
        IRValue fieldEq = emitEqValue(it->second.fields[i].type, lhsField, rhsField);
        if(fieldEq.type == nullptr) {
            return;
        }

        result = emitBinaryOp(IROp::And, result, fieldEq);
    }

    emitRet(result);
}

void Lower::generateArrayEqHelper(const SemTypePtr &type) {
    if(!type || type->kind != SemKind::Array || type->elem == nullptr) {
        return;
    }

    IRType *arrayType = irType(type);
    if(arrayType == nullptr) {
        return;
    }

    IRValue lhs = curFunc->params[0];
    IRValue rhs = curFunc->params[1];
    IRValue zero = makeIRInt(0, types.i32());
    IRValue one = makeIRInt(1, types.i32());

    IRValue indexSlot = emitAlloca(types.i32());
    emitStore(makeIRInt(0, types.i32()), indexSlot, types.i32());
    IRValue okSlot = emitAlloca(types.i1());
    emitStore(makeIRInt(1, types.i1()), okSlot, types.i1());

    size_t loopBlock = newBlock("eq.loop");
    size_t bodyBlock = newBlock("eq.body");
    size_t doneBlock = newBlock("eq.done");

    emitBr(loopBlock);

    switchBlock(loopBlock);
    IRValue index = emitLoad(types.i32(), indexSlot);
    IRValue cond = emitICmp(IRCmp::Ult, index,
                            makeIRInt(static_cast<long long>(type->arrayLen), types.i32()));
    emitCondBr(cond, bodyBlock, doneBlock);

    switchBlock(bodyBlock);
    std::vector<IRValue> indices{zero, index};
    IRValue lhsElem = emitGep(arrayType, lhs, indices);
    IRValue rhsElem = emitGep(arrayType, rhs, indices);
    IRValue elemEq = emitEqValue(type->elem, lhsElem, rhsElem);
    if(elemEq.type == nullptr) {
        return;
    }

    IRValue ok = emitLoad(types.i1(), okSlot);
    IRValue nextOk = emitBinaryOp(IROp::And, ok, elemEq);
    emitStore(nextOk, okSlot, types.i1());

    IRValue nextIndex = emitBinaryOp(IROp::Add, index, one);
    emitStore(nextIndex, indexSlot, types.i32());
    emitBr(loopBlock);

    switchBlock(doneBlock);
    emitRet(emitLoad(types.i1(), okSlot));
}

void Lower::generateVecEqHelper(const SemTypePtr &type) {
    if(!type || type->kind != SemKind::Vec || type->inner == nullptr) {
        return;
    }

    IRType *headerType = irType(type);
    IRType *elemType = irType(type->inner);
    if(headerType == nullptr || elemType == nullptr) {
        return;
    }

    IRValue lhs = curFunc->params[0];
    IRValue rhs = curFunc->params[1];
    IRValue zero = makeIRInt(0, types.i32());
    IRValue one = makeIRInt(1, types.i32());

    std::vector<IRValue> lenIndices{zero, one};
    IRValue lenL = emitLoad(types.i32(), emitGep(headerType, lhs, lenIndices));
    IRValue lenR = emitLoad(types.i32(), emitGep(headerType, rhs, lenIndices));
    IRValue lenEq = emitICmp(IRCmp::Eq, lenL, lenR);

    IRValue indexSlot = emitAlloca(types.i32());
    emitStore(makeIRInt(0, types.i32()), indexSlot, types.i32());
    IRValue okSlot = emitAlloca(types.i1());
    emitStore(makeIRInt(1, types.i1()), okSlot, types.i1());

    size_t falseBlock = newBlock("eq.false");
    size_t lenOkBlock = newBlock("eq.len_ok");
    size_t loopBlock = newBlock("eq.loop");
    size_t bodyBlock = newBlock("eq.body");
    size_t doneBlock = newBlock("eq.done");

    emitCondBr(lenEq, lenOkBlock, falseBlock);

    switchBlock(falseBlock);
    emitRet(makeIRInt(0, types.i1()));

    switchBlock(lenOkBlock);
    std::vector<IRValue> dataIndices{zero, zero};
    IRValue dataL = emitLoad(types.ptr(), emitGep(headerType, lhs, dataIndices));
    IRValue dataR = emitLoad(types.ptr(), emitGep(headerType, rhs, dataIndices));
    emitBr(loopBlock);

    switchBlock(loopBlock);
    IRValue index = emitLoad(types.i32(), indexSlot);
    IRValue cond = emitICmp(IRCmp::Ult, index, lenL);
    emitCondBr(cond, bodyBlock, doneBlock);

    switchBlock(bodyBlock);
    std::vector<IRValue> elemIndices{index};
    IRValue lhsElem = emitGep(elemType, dataL, elemIndices);
    IRValue rhsElem = emitGep(elemType, dataR, elemIndices);
    IRValue elemEq = emitEqValue(type->inner, lhsElem, rhsElem);
    if(elemEq.type == nullptr) {
        return;
    }

    IRValue ok = emitLoad(types.i1(), okSlot);
    IRValue nextOk = emitBinaryOp(IROp::And, ok, elemEq);
    emitStore(nextOk, okSlot, types.i1());

    IRValue nextIndex = emitBinaryOp(IROp::Add, index, one);
    emitStore(nextIndex, indexSlot, types.i32());
    emitBr(loopBlock);

    switchBlock(doneBlock);
    emitRet(emitLoad(types.i1(), okSlot));
}

void Lower::generateRefEqHelper(const SemTypePtr &type) {
    if(!type || type->kind != SemKind::Ref || type->inner == nullptr) {
        return;
    }

    IRValue lhsPtr = emitLoad(types.ptr(), curFunc->params[0]);
    IRValue rhsPtr = emitLoad(types.ptr(), curFunc->params[1]);
    IRValue eq = emitEqValue(type->inner, lhsPtr, rhsPtr);
    if(eq.type != nullptr) {
        emitRet(eq);
    }
}

void Lower::generateBoxEqHelper(const SemTypePtr &type) {
    if(!type || type->kind != SemKind::Box || type->inner == nullptr) {
        return;
    }

    IRValue lhsPtr = emitLoad(types.ptr(), curFunc->params[0]);
    IRValue rhsPtr = emitLoad(types.ptr(), curFunc->params[1]);
    IRValue eq = emitEqValue(type->inner, lhsPtr, rhsPtr);
    if(eq.type != nullptr) {
        emitRet(eq);
    }
}

IRValue Lower::lowerEqAddress(Expr *expr, SemTypePtr type) {
    if(type->kind == SemKind::Struct || type->kind == SemKind::Array || type->kind == SemKind::Vec) {
        //aggregate
        return lowerExpr(expr, nullptr);
    }
    else if(type->kind == SemKind::Ref || type->kind == SemKind::Box) {
        auto addr = lowerPlace(expr);
        if(addr.type != nullptr) return addr;

        auto val = lowerExpr(expr, nullptr);
        if(terminated() || val.type == nullptr) return {};

        auto slot = emitAlloca(types.ptr());
        emitStore(val, slot, types.ptr());
        return slot;
    }
    return {};
}

IRValue Lower::lowerEquality(BinaryExpr *expr) {
    if(expr == nullptr) return {};

    auto lhsType = sema.typeOf(expr->lhs.get());
    auto rhsType = sema.typeOf(expr->rhs.get());
    if(!lhsType || !rhsType || lhsType->isError() || rhsType->isError()) {
        return {};
    }

    SemTypePtr lhsTarget = lhsType;
    SemTypePtr rhsTarget = rhsType;
    while(lhsTarget && (lhsTarget->kind == SemKind::Ref ||
                        lhsTarget->kind == SemKind::Box)) {
        lhsTarget = lhsTarget->inner;
    }
    while(rhsTarget && (rhsTarget->kind == SemKind::Ref ||
                        rhsTarget->kind == SemKind::Box)) {
        rhsTarget = rhsTarget->inner;
    }

    if(!lhsTarget || !rhsTarget || !sameType(lhsTarget, rhsTarget)) {
        return {};
    }

    bool scalar = lhsTarget->isInteger() || lhsTarget->isBool();
    if(!scalar) {
        IRValue lhsAddr = lowerEqAddress(expr->lhs.get(), lhsType);
        if(lhsAddr.type == nullptr) return {};

        IRValue rhsAddr = lowerEqAddress(expr->rhs.get(), rhsType);
        if(rhsAddr.type == nullptr) return {};

        IRValue eq = emitEqCall(lhsType, lhsAddr, rhsAddr);
        if(eq.type == nullptr) return {};

        if(expr->op == BinaryOp::Ne) {
            return emitBinaryOp(IROp::Xor, eq, makeIRInt(1, types.i1()));
        }
        return eq;
    }

    IRValue lhs = lowerExpr(expr->lhs.get(), nullptr);
    if(terminated() || lhs.type == nullptr) return {};

    IRValue rhs = lowerExpr(expr->rhs.get(), nullptr);
    if(terminated() || rhs.type == nullptr) return {};

    while(lhsType && (lhsType->kind == SemKind::Ref ||
                      lhsType->kind == SemKind::Box)) {
        lhsType = lhsType->inner;
        IRType *type = irType(lhsType);
        if(type == nullptr || type->kind == IRKind::Void) return {};
        lhs = emitLoad(type, lhs);
    }
    while(rhsType && (rhsType->kind == SemKind::Ref ||
                      rhsType->kind == SemKind::Box)) {
        rhsType = rhsType->inner;
        IRType *type = irType(rhsType);
        if(type == nullptr || type->kind == IRKind::Void) return {};
        rhs = emitLoad(type, rhs);
    }

    IRCmp cmp = expr->op == BinaryOp::Eq ? IRCmp::Eq : IRCmp::Ne;
    return emitICmp(cmp, lhs, rhs);
}

IRValue Lower::lowerOrdering(BinaryExpr *expr) {
    if(expr == nullptr) return {};

    auto lhsType = sema.typeOf(expr->lhs.get());
    auto rhsType = sema.typeOf(expr->rhs.get());
    if(!lhsType || !rhsType || lhsType->isError() || rhsType->isError()) {
        return {};
    }

    IRValue lhs = lowerExpr(expr->lhs.get(), nullptr);
    if(terminated() || lhs.type == nullptr) return {};

    IRValue rhs = lowerExpr(expr->rhs.get(), nullptr);
    if(terminated() || rhs.type == nullptr) return {};

    while(lhsType && lhsType->kind == SemKind::Ref) {
        lhsType = lhsType->inner;
        IRType *type = irType(lhsType);
        if(type == nullptr || type->kind == IRKind::Void) return {};
        lhs = emitLoad(type, lhs);
    }
    while(rhsType && rhsType->kind == SemKind::Ref) {
        rhsType = rhsType->inner;
        IRType *type = irType(rhsType);
        if(type == nullptr || type->kind == IRKind::Void) return {};
        rhs = emitLoad(type, rhs);
    }

    if(!lhsType || !rhsType ||
       (!lhsType->isInteger() && !lhsType->isBool()) ||
       (!rhsType->isInteger() && !rhsType->isBool()) ||
       !sameType(lhsType, rhsType)) {
        return {};
    }

    bool isSigned = lhsType->isSignedInteger();
    IRCmp cmp;
    switch(expr->op) {
        case BinaryOp::Lt: cmp = isSigned ? IRCmp::Slt : IRCmp::Ult; break;
        case BinaryOp::Le: cmp = isSigned ? IRCmp::Sle : IRCmp::Ule; break;
        case BinaryOp::Gt: cmp = isSigned ? IRCmp::Sgt : IRCmp::Ugt; break;
        case BinaryOp::Ge: cmp = isSigned ? IRCmp::Sge : IRCmp::Uge; break;
        default: return {};
    }

    return emitICmp(cmp, lhs, rhs);
}

IRValue Lower::lowerLogic(BinaryExpr *expr) {
    if(expr == nullptr) return {};
    if(expr->op != BinaryOp::LogicalAnd &&
       expr->op != BinaryOp::LogicalOr) {
        return {};
    }

    IRValue lhs = lowerExpr(expr->lhs.get(), nullptr);
    if(terminated() || lhs.type == nullptr) return {};

    IRValue slot = emitAlloca(types.i1());
    size_t rhsBlock = newBlock("logic.rhs");
    size_t shortBlock = newBlock("logic.short");
    size_t mergeBlock = newBlock("logic.merge");

    bool isAnd = expr->op == BinaryOp::LogicalAnd;
    if(isAnd) {
        emitCondBr(lhs, rhsBlock, shortBlock);
    }
    else {
        emitCondBr(lhs, shortBlock, rhsBlock);
    }

    switchBlock(shortBlock);
    emitStore(makeIRInt(isAnd ? 0 : 1, types.i1()), slot, types.i1());
    emitBr(mergeBlock);

    switchBlock(rhsBlock);
    IRValue rhs = lowerExpr(expr->rhs.get(), nullptr);
    if(terminated() || rhs.type == nullptr) return {};
    emitStore(rhs, slot, types.i1());
    emitBr(mergeBlock);

    switchBlock(mergeBlock);
    return emitLoad(types.i1(), slot);
}

IRValue Lower::lowerArithmeticOrShift(BinaryExpr *expr) {
    if(expr == nullptr) return {};

    auto lhsType = sema.typeOf(expr->lhs.get());
    if(lhsType && lhsType->kind == SemKind::Ref) {
        lhsType = lhsType->inner;
    }
    if(!lhsType || lhsType->isError()) return {};

    IRValue lhs = lowerBinaryOperand(expr->lhs.get());
    if(terminated() || lhs.type == nullptr) return {};

    IRValue rhs = lowerBinaryOperand(expr->rhs.get());
    if(terminated() || rhs.type == nullptr) return {};

    switch(expr->op) {
        case BinaryOp::Add: {
            return emitBinaryOp(IROp::Add, lhs, rhs);
        }
        case BinaryOp::Sub: {
            return emitBinaryOp(IROp::Sub, lhs, rhs);
        }
        case BinaryOp::Mul: {
            return emitBinaryOp(IROp::Mul, lhs, rhs);
        }
        case BinaryOp::Div: {
            return emitBinaryOp(lhsType->isSignedInteger() ? IROp::SDiv : IROp::UDiv, lhs, rhs);
        }
        case BinaryOp::Mod: {
            return emitBinaryOp(lhsType->isSignedInteger() ? IROp::SRem : IROp::URem, lhs, rhs);
        }
        case BinaryOp::BitAnd: {
            return emitBinaryOp(IROp::And, lhs, rhs);
        }
        case BinaryOp::BitOr: {
            return emitBinaryOp(IROp::Or, lhs, rhs);
        }
        case BinaryOp::BitXor: {
            return emitBinaryOp(IROp::Xor, lhs, rhs);
        }
        case BinaryOp::Shl: {
            IRValue count = emitBinaryOp(IROp::And, rhs, makeIRInt(31, rhs.type));
            return emitBinaryOp(IROp::Shl, lhs, count);
        }
        case BinaryOp::Shr: {
            IRValue count = emitBinaryOp(IROp::And, rhs, makeIRInt(31, rhs.type));
            return emitBinaryOp(lhsType->isSignedInteger() ? IROp::AShr : IROp::LShr,
                                lhs, count);
        }
        default:
            return {};
    }
}

IRValue Lower::lowerBinaryExpr(BinaryExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    if(expr == nullptr) return {};

    switch(expr->op) {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod:
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor:
        case BinaryOp::Shl:
        case BinaryOp::Shr: {
            return lowerArithmeticOrShift(expr);
        }
        case BinaryOp::Eq:
        case BinaryOp::Ne: {
            return lowerEquality(expr);
        }
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge: {
            return lowerOrdering(expr);
        }
        case BinaryOp::LogicalAnd:
        case BinaryOp::LogicalOr: {
            return lowerLogic(expr);
        }
    }
    return {};
}

IRValue Lower::emitCompoundAssign(AssignOp op, IRValue lhs, IRValue rhs, SemTypePtr type) {
    switch(op) {
        case AssignOp::Add: {
            return emitBinaryOp(IROp::Add, lhs, rhs);
        }
        case AssignOp::Sub: {
            return emitBinaryOp(IROp::Sub, lhs, rhs);
        }
        case AssignOp::Mul: {
            return emitBinaryOp(IROp::Mul, lhs, rhs);
        }
        case AssignOp::Div: {
            return emitBinaryOp(type->isSignedInteger() ? IROp::SDiv : IROp::UDiv, lhs, rhs);
        }
        case AssignOp::Mod: {
            return emitBinaryOp(type->isSignedInteger() ? IROp::SRem : IROp::URem, lhs, rhs);
        }
        case AssignOp::BitAnd: {
            return emitBinaryOp(IROp::And, lhs, rhs);
        }
        case AssignOp::BitOr: {
            return emitBinaryOp(IROp::Or, lhs, rhs);
        }
        case AssignOp::BitXor: {
            return emitBinaryOp(IROp::Xor, lhs, rhs);
        }
        case AssignOp::Shl: {
            IRValue count = emitBinaryOp(IROp::And, rhs, makeIRInt(31, rhs.type));
            return emitBinaryOp(IROp::Shl, lhs, count);
        }
        case AssignOp::Shr: {
            IRValue count = emitBinaryOp(IROp::And, rhs, makeIRInt(31, rhs.type));
            return emitBinaryOp(type->isSignedInteger() ? IROp::AShr : IROp::LShr,
                                lhs, count);
        }
        default: {
            return {};
        }
    }
}

IRValue Lower::lowerAssignExpr(AssignExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    auto lhsType = sema.typeOf(expr->lhs.get());
    if(!lhsType || lhsType->isError()) return {};

    if(expr->op == AssignOp::Assign) {
        auto rhs = lowerExpr(expr->rhs.get(), lhsType);
        if(terminated() || rhs.type == nullptr) return {};
        rhs = coerce(rhs, sema.typeOf(expr->rhs.get()), lhsType);
        if(terminated() || rhs.type == nullptr) return {};

        auto lhsAddr = lowerPlace(expr->lhs.get());
        if(lhsAddr.type == nullptr) return {};
        auto typ = irType(lhsType);
        if(typ == nullptr || typ->kind == IRKind::Void) return {};
        if(isAggregateType(typ)) {
            emitMemmove(lhsAddr, rhs, makeIRInt(typ->size));
        }
        else {
            emitStore(rhs, lhsAddr, typ);
        }
        return {};
    }
    else {
        //spec: rhs->lhs->deref rhs
        auto rhsType = sema.typeOf(expr->rhs.get());
        if(!rhsType || rhsType->isError()) return {};

        auto rhs = lowerExpr(expr->rhs.get(), nullptr);
        if(terminated() || rhs.type == nullptr) return {};

        auto lhsAddr = lowerPlace(expr->lhs.get());
        if(lhsAddr.type == nullptr) return {};

        auto typ = irType(lhsType);
        if(typ == nullptr || typ->kind == IRKind::Void) return {};

        if(rhsType->kind == SemKind::Ref) {
            if(rhsType->isMut) return {};
            IRType *innerType = irType(rhsType->inner);
            if(innerType == nullptr || innerType->kind == IRKind::Void) return {};
            rhs = emitLoad(innerType, rhs);
        }

        auto val = emitLoad(typ, lhsAddr);
        auto ret = emitCompoundAssign(expr->op, val, rhs, lhsType);
        if(ret.type == nullptr) return {};
        emitStore(ret, lhsAddr, typ);
        return {};
    }
}

IRValue Lower::lowerFieldExpr(FieldExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    IRValue addr = lowerFieldAddress(expr);
    if(addr.type == nullptr) return {};

    auto resultType = sema.typeOf(expr);
    if(!resultType || resultType->isError()) return {};

    IRType *resultIR = irType(resultType);
    if(resultIR == nullptr || resultIR->kind == IRKind::Void) return {};

    if(isAggregateType(resultIR)) {
        return addr;
    }
    return emitLoad(resultIR, addr);
}

IRValue Lower::lowerReturnExpr(ReturnExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    if(curFuncIsMain) {
        if(expr->val) {
            auto v = lowerExpr(expr->val.get(), curFuncRet);
            (void)v;
            if(terminated()) return {};
        }
        emitRet(makeIRInt(0, types.i32()));
        return {};
    }

    if(curAggRet) {
        if(!expr->val) {
            assert(false);
            return {};
        }
        IRValue val = lowerExpr(expr->val.get(), curFuncRet);
        if(terminated()) return {};
        val = coerce(val, sema.typeOf(expr->val.get()), curFuncRet);
        emitSretReturn(curSret, val, irType(curFuncRet));
        return {};
    }

    if(!curFuncRet || curFuncRet->kind == SemKind::Unit) {
        if(expr->val) {
            auto v = lowerExpr(expr->val.get(), curFuncRet);
            (void)v;
            if(terminated()) return {};
        }
        emitRet();
        return {};
    }

    if(!expr->val) {
        assert(false);
        return {};
    }
    IRValue val = lowerExpr(expr->val.get(), curFuncRet);
    if(terminated()) return {};
    val = coerce(val, sema.typeOf(expr->val.get()), curFuncRet);
    emitRet(val);
    return {};
}

IRValue Lower::lowerBreakExpr(BreakExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    if(loopStack.empty()) return {};

    LoopCtx &ctx = loopStack.back();
    if(expr->val == nullptr) {
        ctx.exitReachable = true;
        emitBr(ctx.breakBlock);
        return {};
    }

    if(ctx.isWhile || !ctx.hasResult || !ctx.resultType) {
        return {};
    }

    IRValue value = lowerExpr(expr->val.get(), ctx.resultType);
    if(terminated() || value.type == nullptr) return {};

    value = coerce(value, sema.typeOf(expr->val.get()), ctx.resultType);
    if(terminated() || value.type == nullptr) return {};

    IRType *resultIR = irType(ctx.resultType);
    if(resultIR == nullptr || resultIR->kind == IRKind::Void) return {};

    if(isAggregateType(resultIR)) {
        emitMemcpy(ctx.resultSlot, value, resultIR->size);
    }
    else {
        emitStore(value, ctx.resultSlot, resultIR);
    }

    ctx.exitReachable = true;
    emitBr(ctx.breakBlock);
    return {};
}

IRValue Lower::lowerContinueExpr(ContinueExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    if(loopStack.empty()) return {};
    emitBr(loopStack.back().continueBlock);
    return {};
}

IRValue Lower::lowerIfExpr(IfExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    auto cond = lowerExpr(expr->cond.get(), SemType::make(SemKind::Bool));
    if(terminated() || cond.type == nullptr) return {};

    auto resultType = sema.typeOf(expr);
    if(!resultType || resultType->isError()) return {};

    bool hasResult = resultType->kind != SemKind::Unit &&
                     resultType->kind != SemKind::Never;
    IRType *resultIR = nullptr;
    IRValue slot;
    if(hasResult) {
        resultIR = irType(resultType);
        if(resultIR == nullptr || resultIR->kind == IRKind::Void) {
            return {};
        }
        slot = emitAlloca(resultIR);
    }

    size_t thenBlock = newBlock("if.then");
    size_t mergeBlock = newBlock("if.merge");
    size_t elseBlock = expr->elseBranch ? newBlock("if.else") : mergeBlock;

    emitCondBr(cond, thenBlock, elseBlock);

    bool reachesMerge = !expr->elseBranch;

    switchBlock(thenBlock);
    IRValue thenVal = lowerBlockExpr(expr->thenBlock.get(),
                                     hasResult ? resultType : nullptr);
    if(!terminated()) {
        if(hasResult) {
            if(thenVal.type == nullptr) return {};
            SemTypePtr thenType = expr->thenBlock->tail ? sema.typeOf(expr->thenBlock->tail.get()) : nullptr;
            thenVal = coerce(thenVal, thenType, resultType);
            if(terminated() || thenVal.type == nullptr) return {};

            if(isAggregateType(resultIR)) {
                emitMemcpy(slot, thenVal, resultIR->size);
            }
            else {
                emitStore(thenVal, slot, resultIR);
            }
        }
        emitBr(mergeBlock);
        reachesMerge = true;
    }

    if(expr->elseBranch) {
        switchBlock(elseBlock);
        IRValue elseVal = lowerExpr(expr->elseBranch.get(), hasResult ? resultType : nullptr);
        if(!terminated()) {
            if(hasResult) {
                if(elseVal.type == nullptr) return {};
                elseVal = coerce(elseVal, sema.typeOf(expr->elseBranch.get()), resultType);
                if(terminated() || elseVal.type == nullptr) return {};

                if(isAggregateType(resultIR)) {
                    emitMemcpy(slot, elseVal, resultIR->size);
                }
                else {
                    emitStore(elseVal, slot, resultIR);
                }
            }
            emitBr(mergeBlock);
            reachesMerge = true;
        }
    }

    if(!reachesMerge) {
        switchBlock(mergeBlock);
        emitUnreachable();
        return {};
    }

    switchBlock(mergeBlock);
    if(!hasResult) {
        return {};
    }

    if(isAggregateType(resultIR)) {
        return slot;
    }
    return emitLoad(resultIR, slot);
}

IRValue Lower::lowerWhileExpr(WhileExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    auto resultType = sema.typeOf(expr);
    if(!resultType || resultType->isError()) {
        return {};
    }

    size_t condBlock = newBlock("while.cond");
    size_t bodyBlock = newBlock("while.body");
    size_t exitBlock = newBlock("while.exit");

    emitBr(condBlock);
    switchBlock(condBlock);
    auto cond = lowerExpr(expr->cond.get(), SemType::make(SemKind::Bool));
    if(terminated()) {
        switchBlock(bodyBlock);
        emitUnreachable();
        switchBlock(exitBlock);
        emitUnreachable();
        return {};
    }
    if(cond.type == nullptr) {
        if(!terminated()) {
            emitUnreachable();
        }
        switchBlock(bodyBlock);
        emitUnreachable();
        switchBlock(exitBlock);
        emitUnreachable();
        return {};
    }

    emitCondBr(cond, bodyBlock, exitBlock);
    switchBlock(bodyBlock);
    LoopCtx ctx;
    ctx.breakBlock = exitBlock;
    ctx.continueBlock = condBlock;
    ctx.isWhile = true;
    ctx.hasResult = false;
    ctx.exitReachable = true;
    ctx.resultSlot = {};
    ctx.resultType = nullptr;
    loopStack.push_back(ctx);

    lowerBlockExpr(expr->body.get(), SemType::makeUnit());
    loopStack.pop_back();

    if(!terminated()) {
        emitBr(condBlock);
    }

    switchBlock(exitBlock);
    return {};
}

IRValue Lower::lowerLoopExpr(LoopExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    auto resultType = sema.typeOf(expr);
    if(!resultType || resultType->isError()) return {};

    bool hasResult = resultType->kind != SemKind::Unit &&
                     resultType->kind != SemKind::Never;
    IRType *resultIR = nullptr;
    IRValue resultSlot;
    if(hasResult) {
        resultIR = irType(resultType);
        if(resultIR == nullptr || resultIR->kind == IRKind::Void) {
            return {};
        }
        resultSlot = emitAlloca(resultIR);
    }

    size_t entryBlock = curBlock;
    size_t bodyBlock = newBlock("loop.body");
    size_t exitBlock = newBlock("loop.exit");

    switchBlock(entryBlock);
    emitBr(bodyBlock);
    switchBlock(bodyBlock);

    LoopCtx ctx;
    ctx.breakBlock = exitBlock;
    ctx.continueBlock = bodyBlock;
    ctx.isWhile = false;
    ctx.hasResult = hasResult;
    ctx.exitReachable = false;
    ctx.resultSlot = resultSlot;
    ctx.resultType = resultType;
    loopStack.push_back(ctx);

    lowerBlockExpr(expr->body.get(), SemType::makeUnit());

    bool exitReachable = loopStack.back().exitReachable;
    loopStack.pop_back();

    if(!terminated()) {
        emitBr(bodyBlock);
    }

    switchBlock(exitBlock);

    if(!exitReachable) {
        emitUnreachable();
        return {};
    }

    if(!hasResult) {
        return {};
    }

    if(isAggregateType(resultIR)) {
        return resultSlot;
    }
    return emitLoad(resultIR, resultSlot);
}

IRValue Lower::lowerStructExpr(StructExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    auto *sym = sema.symbolOf(expr);
    if(!sym || sym->decl == nullptr) return {};

    auto *def = static_cast<const StructDefItem*>(sym->decl);
    auto it = sema.structs.find(def);
    if(it == sema.structs.end()) return {};

    auto *typ = irType(sema.typeOf(expr));
    if(typ == nullptr || typ->kind == IRKind::Void) return {};

    auto &info = it->second;
    auto slot = emitAlloca(typ);
    IRValue zero = makeIRInt(0, types.i32());

    for(auto &field: expr->fields) {
        auto fieldIt = info.fieldIndex.find(field->name);
        if(fieldIt == info.fieldIndex.end()) return {};

        size_t index = fieldIt->second;
        const auto &fieldType = info.fields[index].type;
        IRType *fieldIR = irType(fieldType);
        if(fieldIR == nullptr || fieldIR->kind == IRKind::Void) return {};

        IRValue val = lowerExpr(field->value.get(), fieldType);
        if(terminated() || val.type == nullptr) return {};

        val = coerce(val, sema.typeOf(field->value.get()), fieldType);
        if(terminated() || val.type == nullptr) return {};

        IRValue fieldAddr = emitGep(typ, slot, {
            zero,
            makeIRInt(static_cast<long long>(index), types.i32())
        });

        if(isAggregateType(fieldIR)) {
            emitMemcpy(fieldAddr, val, fieldIR->size);
        }
        else {
            emitStore(val, fieldAddr, fieldIR);
        }
    }

    return slot;
}

IRValue Lower::lowerArrayExpr(ArrayExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    auto resultType = sema.typeOf(expr);
    if(!resultType || resultType->kind != SemKind::Array) {
        return {};
    }

    IRType *arrayIR = irType(resultType);
    if(arrayIR == nullptr || arrayIR->kind != IRKind::Array) {
        return {};
    }

    IRType *elemIR = arrayIR->elem;
    if(elemIR == nullptr || elemIR->kind == IRKind::Void) {
        return {};
    }

    IRValue slot = emitAlloca(arrayIR);
    IRValue zero = makeIRInt(0, types.i32());

    if(expr->isRepeat) {
        IRValue value = lowerExpr(expr->repeatValue.get(), resultType->elem);
        if(terminated() || value.type == nullptr) return {};

        value = coerce(value, sema.typeOf(expr->repeatValue.get()), resultType->elem);
        if(terminated() || value.type == nullptr) return {};

        IRValue firstAddr = emitGep(arrayIR, slot, {zero, zero});
        if(isAggregateType(elemIR)) {
            emitMemcpy(firstAddr, value, elemIR->size);
        }
        else {
            emitStore(value, firstAddr, elemIR);
        }

        for(size_t i = 1; i < resultType->arrayLen; i++) {
            IRValue elemAddr = emitGep(arrayIR, slot, {
                zero,
                makeIRInt(static_cast<long long>(i), types.i32())
            });
            emitMemcpy(elemAddr, firstAddr, elemIR->size);
        }

        return slot;
    }

    for(size_t i = 0; i < expr->elems.size(); i++) {
        auto *elemExpr = expr->elems[i].get();
        IRValue value = lowerExpr(elemExpr, resultType->elem);
        if(terminated() || value.type == nullptr) return {};

        value = coerce(value, sema.typeOf(elemExpr), resultType->elem);
        if(terminated() || value.type == nullptr) return {};

        IRValue elemAddr = emitGep(arrayIR, slot, {
            zero,
            makeIRInt(static_cast<long long>(i), types.i32())
        });

        if(isAggregateType(elemIR)) {
            emitMemcpy(elemAddr, value, elemIR->size);
        }
        else {
            emitStore(value, elemAddr, elemIR);
        }
    }

    return slot;
}

IRValue Lower::lowerAggregateBase(Expr *base, SemTypePtr &type) {
    if(base == nullptr) return {};

    type = sema.typeOf(base);
    if(!type || type->isError()) return {};

    IRValue addr = lowerExpr(base, nullptr);
    if(terminated() || addr.type == nullptr) return {};

    SemTypePtr cur = type;
    while(cur && (cur->kind == SemKind::Ref || cur->kind == SemKind::Box)) {
        SemTypePtr inner = cur->inner;
        if(!inner) return {};

        if(inner->kind == SemKind::Ref || inner->kind == SemKind::Box) {
            addr = emitLoad(types.ptr(), addr);
            cur = inner;
        }
        else {
            cur = inner;
            break;
        }
    }

    type = cur;
    return addr;
}

IRValue Lower::lowerFieldAddress(FieldExpr *expr) {
    if(expr == nullptr) return {};

    SemTypePtr baseType;
    IRValue addr = lowerAggregateBase(expr->base.get(), baseType);
    if(addr.type == nullptr || !baseType || baseType->kind != SemKind::Struct) {
        return {};
    }

    auto it = sema.structs.find(baseType->structDef);
    if(it == sema.structs.end()) return {};

    auto fieldIt = it->second.fieldIndex.find(expr->field);
    if(fieldIt == it->second.fieldIndex.end()) return {};

    IRType *structIR = irType(baseType);
    if(structIR == nullptr || structIR->kind != IRKind::Struct) return {};

    return emitGep(structIR, addr, {
        makeIRInt(0, types.i32()),
        makeIRInt(static_cast<long long>(fieldIt->second), types.i32())
    });
}

IRValue Lower::lowerIndexAddress(IndexExpr *expr) {
    if(expr == nullptr) return {};

    SemTypePtr cur;
    IRValue addr = lowerAggregateBase(expr->base.get(), cur);
    if(addr.type == nullptr || !cur) return {};

    IRValue index = lowerExpr(expr->index.get(), SemType::make(SemKind::Usize));
    if(terminated() || index.type == nullptr) return {};

    if(cur->kind == SemKind::Array) {
        IRType *arrayIR = irType(cur);
        if(arrayIR == nullptr || arrayIR->kind != IRKind::Array) return {};

        return emitGep(arrayIR, addr, {
            makeIRInt(0, types.i32()),
            index
        });
    }

    if(cur->kind == SemKind::Vec) {
        IRType *headerIR = types.vecHeader();
        IRValue dataAddr = emitGep(headerIR, addr, {
            makeIRInt(0, types.i32()),
            makeIRInt(0, types.i32())
        });
        IRValue dataPtr = emitLoad(types.ptr(), dataAddr);

        IRType *elemIR = irType(cur->inner);
        if(elemIR == nullptr || elemIR->kind == IRKind::Void) return {};

        return emitGep(elemIR, dataPtr, {index});
    }

    return {};
}

IRValue Lower::lowerIndexExpr(IndexExpr *expr, const SemTypePtr &expected) {
    (void)expected;

    IRValue addr = lowerIndexAddress(expr);
    if(addr.type == nullptr) return {};

    auto resultType = sema.typeOf(expr);
    if(!resultType || resultType->isError()) return {};

    IRType *resultIR = irType(resultType);
    if(resultIR == nullptr || resultIR->kind == IRKind::Void) return {};

    if(isAggregateType(resultIR)) {
        return addr;
    }
    return emitLoad(resultIR, addr);
}

IRValue Lower::lowerBuiltinIO(CallExpr *expr, Symbol *callee, const FuncSig &sig, BuiltinOp op) {
    (void)callee;
    switch(op) {
        case BuiltinOp::PrintI32: {
            IRInstr call;
            call.op = IROp::Call;
            call.type = types.i32();
            call.varArg = true;
            call.hasDst = false;
            call.calleeName = "printf";
            IRValue fmtVal;
            fmtVal.kind = IRValueKind::Global;
            fmtVal.type = types.ptr();
            fmtVal.global = IntFmt;
            call.args.push_back(fmtVal);

            auto val = lowerExpr(expr->args[0].get(), sig.params[0]);
            if(terminated()) return {};
            val = coerce(val, sema.typeOf(expr->args[0].get()), sig.params[0]);
            call.args.push_back(val);
            
            emit(call);
            return {};
        }
        case BuiltinOp::GetI32: {
            auto tmp = emitAlloca(types.i32());
            IRInstr call;
            call.op = IROp::Call;
            call.type = types.i32();
            call.calleeName = "scanf";
            call.varArg = true;
            call.hasDst = false;
            IRValue fmtVal;
            fmtVal.kind = IRValueKind::Global;
            fmtVal.type = types.ptr();
            fmtVal.global = IntFmt;
            call.args.push_back(fmtVal);
            call.args.push_back(tmp);
            emit(call);
            return emitLoad(types.i32(), tmp);
        }
        case BuiltinOp::PrintlnI32: {
            IRInstr call;
            call.op = IROp::Call;
            call.type = types.i32();
            call.varArg = true;
            call.hasDst = false;
            call.calleeName = "printf";
            IRValue fmtVal;
            fmtVal.kind = IRValueKind::Global;
            fmtVal.type = types.ptr();
            fmtVal.global = IntlnFmt;
            call.args.push_back(fmtVal);

            auto val = lowerExpr(expr->args[0].get(), sig.params[0]);
            if(terminated()) return {};
            val = coerce(val, sema.typeOf(expr->args[0].get()), sig.params[0]);
            call.args.push_back(val);
            
            emit(call);
            return {};
        }
        default: return {};
    }
}

IRValue Lower::emitCall(const std::string &calleeName, IRType *retType,
                        const std::vector<IRValue> &args, bool varArg) {
    IRInstr call;
    call.op = IROp::Call;
    call.calleeName = calleeName;
    call.varArg = varArg;

    bool isAggregate = isAggregateType(retType);
    call.type = isAggregate ? types.voidType() : retType;

    IRValue out;
    if(isAggregate) {
        out = emitAlloca(retType);
        call.args.push_back(out);
    }
    else if(retType->kind != IRKind::Void) {
        call.dst = newReg(retType);
        call.hasDst = true;
    }

    call.args.insert(call.args.end(), args.begin(), args.end());
    emit(call);
    return isAggregate ? out : (call.hasDst ? call.dst : IRValue{});
}

IRValue Lower::lowerFuncCall(CallExpr *expr, Symbol *callee, const FuncSig &sig) {
    const StructDefItem *owner = memberOwner(callee);
    std::string calleeName = owner ? owner->name + "." + callee->name : callee->name;

    std::vector<IRValue> args;
    args.reserve(sig.params.size() + (sig.hasSelf ? 1 : 0));
    size_t base = sig.hasSelf ? 1 : 0;

    if(sig.hasSelf) {
        if(expr->args.empty()) return {};
        auto recvType = sig.selfByRef ? SemType::makeRef(callee->type, sig.selfMut) : callee->type;
        auto val = lowerExpr(expr->args[0].get(), recvType);
        if(terminated()) return {};
        val = coerce(val, sema.typeOf(expr->args[0].get()), recvType);
        args.push_back(val);
    }

    for(size_t i = 0; i < sig.params.size(); i++) {
        auto *argExpr = expr->args[i + base].get();
        auto val = lowerExpr(argExpr, sig.params[i]);
        if(terminated()) return {};
        val = coerce(val, sema.typeOf(argExpr), sig.params[i]);
        args.push_back(val);
    }

    return emitCall(calleeName, irType(sig.ret), args, false);
}

IRValue Lower::lowerBuiltinFunc(CallExpr *expr, Symbol *callee, const FuncSig &sig, BuiltinOp op) {
    (void)expr;
    (void)callee;
    (void)sig;
    (void)op;
    return {};
}

IRValue Lower::lowerCallExpr(CallExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    auto sym = sema.symbolOf(expr->callee.get());
    if(sym == nullptr) return {};
    auto sigIt = sema.sigs.find(sym);
    if(sigIt == sema.sigs.end()) return {};

    auto &sig = sigIt->second;

    auto op = sema.builtinOpOf(sym);

    if(op == BuiltinOp::PrintI32 || op == BuiltinOp::PrintlnI32 || op == BuiltinOp::GetI32) {
        return lowerBuiltinIO(expr, sym, sig, op);
    }
    else if(op != BuiltinOp::None) {
        return lowerBuiltinFunc(expr, sym, sig, op);
    }

    if(sym->name == "main" && memberOwner(sym) == nullptr) {//main;
        IRInstr call;
        call.op = IROp::Call;
        call.calleeName = "main";
        call.type = types.i32();
        call.hasDst = false;
        emit(call);
        return {};//Unit
    }

    return lowerFuncCall(expr, sym, sig);
}

IRValue Lower::lowerMethodCallExpr(MethodCallExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerCastExpr(CastExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    auto srcType = sema.typeOf(expr->expr.get());
    auto dstType = sema.typeOf(expr);
    if(!srcType || !dstType || srcType->isError() || dstType->isError()) {
        return {};
    }
    IRValue val = lowerExpr(expr->expr.get(), nullptr);
    if(terminated() || val.type == nullptr) {
        return {};
    }
    if(srcType->isInteger() && dstType->isInteger()) {
        //no-op , i32->i32
        return val;
    }
    if(srcType->isBool() && dstType->isInteger()) {
        return emitZext(val, irType(dstType));
    }
    return {};//invalid
}

IRValue Lower::lowerUnitExpr(UnitExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};//Unit is invalid SSA type
}

IRValue Lower::lowerGroupExpr(GroupExpr *expr, const SemTypePtr &expected) {
    return lowerExpr(expr->inner.get(), expected);
}

IRValue Lower::coerce(IRValue val, const SemTypePtr &src, const SemTypePtr &dst) {
    if(!src || !dst) return val;
    if(src->isError() || dst->isError()) return val;
    if(src->isNever()) return {};
    if(sameType(src, dst)) return val;

    //unit->other: already rejected in semantic
    //not ref coerce: already rejected in semantic
    //not mut->mut: already rejected in semantic

    assert(src->kind == SemKind::Ref && dst->kind == SemKind::Ref);
    assert(src->inner != nullptr && dst->inner != nullptr);
    assert(!(dst->isMut && !src->isMut));

    auto cur = src->inner;
    while(cur) {
        if(sameType(cur, dst->inner)) {
            return val;
        }
        //cur not box or not ref: already rejected in semantic
        if(cur->kind != SemKind::Ref && cur->kind != SemKind::Box) break;
        //not mut->mut: already rejected in semantic
        if(dst->isMut && cur->kind == SemKind::Ref && !cur->isMut) break;

        IRValue x = emitLoad(types.ptr(), val);
        val = x;
        cur = cur->inner;
    }
    assert(false);
    return val;
}

IRGlobal *Lower::getFmtGlobal(const std::string &str, const std::string &name) {
    auto global = std::make_unique<IRGlobal>();
    global->name = name;
    global->bytes.assign(str.begin(), str.end());
    global->bytes.push_back(0);//add the end '\0'
    global->type = types.array(types.i8(), global->bytes.size());
    global->align = 1;

    auto *raw = global.get();
    module.globals.push_back(std::move(global));
    return raw;
}
} // namespace rx::ir
