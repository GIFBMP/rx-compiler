#include "lower.h"
#include "semantic/symbol.h"

#include <utility>

static bool isAggregateType(const IRType *type) {
    return type != nullptr &&
           (type->kind == IRKind::Struct || type->kind == IRKind::Array);
}

IRModule Lower::lower(Crate *crate) {
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

size_t Lower::newBlock(const std::string &name) {
    assert(curFunc != nullptr);
    size_t id = curFunc->blocks.size();
    auto block = std::make_unique<IRBlock>();
    block->name = name;
    curFunc->blocks.push_back(std::move(block));
    return id;
}

void Lower::emit(const IRInstr &instr) {
    assert(curFunc != nullptr);
    assert(curBlock < curFunc->blocks.size());
    assert(!terminated());
    curFunc->blocks[curBlock]->instrs.push_back(std::make_unique<IRInstr>(instr));
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
    IRValue slot = newReg(types.ptr());
    IRInstr instr;
    instr.op = IROp::Alloca;
    instr.type = type;
    instr.dst = slot;
    instr.hasDst = true;
    instr.align = type->align;
    emit(instr);
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

void Lower::emitMemcpy(IRValue dst, IRValue src, IRValue size) {
    IRInstr copy;
    copy.op = IROp::Memcpy;
    copy.args.push_back(dst);
    copy.args.push_back(src);
    copy.args.push_back(size);
    emit(copy);
}

void Lower::emitMemcpy(IRValue dst, IRValue src, size_t size) {
    emitMemcpy(dst, src, IRValue{IRValueKind::Int, types.i32(), 0,
                                 static_cast<long long>(size), nullptr});
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
        emitRet(IRValue{IRValueKind::Int, types.i32(), 0, 0, nullptr});
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
    IRValue ret;
    ret.kind = IRValueKind::Int;
    ret.type = types.i1();
    ret.intValue = expr->val ? 1 : 0;
    return ret;
}

IRValue Lower::lowerIntExpr(IntExpr *expr, const SemTypePtr &expected) {
    //imm number, does not need reg
    (void)expected;
    IRValue ret;
    ret.kind = IRValueKind::Int;
    ret.type = types.i32();
    ret.intValue = static_cast<long long>(expr->val.val);
    return ret;
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

IRValue Lower::lowerUnaryExpr(UnaryExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerBinaryExpr(BinaryExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerAssignExpr(AssignExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerFieldExpr(FieldExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerReturnExpr(ReturnExpr *expr, const SemTypePtr &expected) {
    (void)expected;
    if(curFuncIsMain) {
        if(expr->val) {
            auto v = lowerExpr(expr->val.get(), curFuncRet);
            (void)v;
            if(terminated()) return {};
        }
        emitRet(IRValue{IRValueKind::Int, types.i32(), 0, 0, nullptr});
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
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerContinueExpr(ContinueExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerIfExpr(IfExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerWhileExpr(WhileExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerLoopExpr(LoopExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerStructExpr(StructExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerArrayExpr(ArrayExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerIndexExpr(IndexExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
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
    (void)expr;
    (void)expected;
    return {};
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