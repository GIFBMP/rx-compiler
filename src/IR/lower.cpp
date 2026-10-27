#include "lower.h"

#include <utility>

IRModule Lower::lower(Crate *crate) {
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

    IRValue slot = newReg(types.ptr());
    IRInstr alloc;
    alloc.op = IROp::Alloca;
    alloc.type = ty;
    alloc.dst = slot;
    alloc.hasDst = true;
    alloc.align = ty->align;
    emit(alloc);

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

    auto func = std::make_unique<IRFunc>();
    func->name = owner ? owner->name + "." + item->name : item->name;
    func->retType = isMain ? types.i32() : irType(sig.ret);

    module.funcs.push_back(std::move(func));
    curFunc = module.funcs.back().get();

    nxReg = 0;
    curBlock = 0;
    locals.clear();
    curFuncRet = sig.ret;
    curFuncIsMain = isMain;//re-initialize
    curBlock = newBlock("entry");

    for(auto &p: item->params) {//allocate params
        auto *paramSym = sema.symbolOf(p.get());
        assert(paramSym != nullptr);
        auto *paramType = irType(paramSym->type);
        assert(paramType->kind != IRKind::Void);

        auto reg = newReg(paramType);//imm reg, just for store
        curFunc->params.push_back(reg);

        auto stackSlot = newReg(types.ptr());
        IRInstr alloc;
        alloc.op = IROp::Alloca;
        alloc.type = paramType;
        alloc.dst = stackSlot;
        alloc.hasDst = true;
        alloc.align = paramType->align;
        emit(alloc);
        locals[paramSym] = stackSlot;//alloca

        IRInstr store;
        store.op = IROp::Store;
        store.type = paramType;
        store.args.push_back(reg);
        store.args.push_back(stackSlot);
        emit(store);//store
    }

    //preprocess the letstmt(entry alloca, according to standard llvm)
    preDeclareLocals(item->body.get());

    auto tail = lowerBlockExpr(item->body.get(), sig.ret);
    if(terminated()) return;

    if(item->body) {
        SemTypePtr bodyType = sema.typeOf(item->body.get());
        if(bodyType && bodyType->isNever()) {
            IRInstr unr;
            unr.op = IROp::Unreachable;
            emit(unr);
            return;
        }
    }

    if(isMain) {//ret i32 0
        IRInstr ret;
        ret.op = IROp::Ret;
        ret.type = types.i32();
        ret.args.push_back(IRValue{IRValueKind::Int, types.i32(), 0, 0, nullptr});
        emit(ret);
    }
    else if(!sig.ret || sig.ret->kind == SemKind::Unit) {
        IRInstr ret;
        ret.op = IROp::Ret;
        ret.type = types.voidType();
        emit(ret);
    }
    else {
        IRInstr ret;
        ret.op = IROp::Ret;
        ret.type = irType(sig.ret);
        IRValue val = coerce(tail, item->body->tail ? sema.typeOf(item->body->tail.get()) : nullptr, sig.ret);
        ret.args.push_back(val);
        emit(ret);
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

    IRInstr store;
    store.op = IROp::Store;
    store.type = typ;
    store.args.push_back(val);
    store.args.push_back(slot);
    emit(store);

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
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerIntExpr(IntExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerBlockExpr(BlockExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::lowerPathExpr(PathExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
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
    (void)expr;
    (void)expected;
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

IRValue Lower::lowerCallExpr(CallExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
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
    return {};
}

IRValue Lower::lowerGroupExpr(GroupExpr *expr, const SemTypePtr &expected) {
    (void)expr;
    (void)expected;
    return {};
}

IRValue Lower::coerce(IRValue val, const SemTypePtr &src, const SemTypePtr &dst) {
    (void)src;
    (void)dst;
    return val;
}
