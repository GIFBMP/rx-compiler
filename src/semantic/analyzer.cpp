#include "analyzer.h"
#include "AST/type.h"
#include <utility>
#include <unordered_map>
#include <unordered_set>

bool SemanticAnalyzer::analyze(Crate *crate) {
    if(crate == nullptr) return false;
    initBuiltins();
    constEval_.setAnalyzer(this);
    collectItems(crate);
    return !hasError();
}

bool SemanticAnalyzer::hasError() const {
    return !diags.empty();
}

const std::vector<Diagnostic> &SemanticAnalyzer::diagnostics() const {
    return diags;
}

Symbol *SemanticAnalyzer::lookupType(const std::string &name) const {
    auto it = types_.find(name);
    return it == types_.end() ? nullptr : it->second;
}

Symbol *SemanticAnalyzer::lookupValue(const std::string &name) const {
    auto it = globals_.find(name);
    return it == globals_.end() ? nullptr : it->second;
}

const StructInfo *SemanticAnalyzer::structInfo(const StructDefItem *def) const {
    auto it = structs_.find(def);
    return it == structs_.end() ? nullptr : &it->second;
}

const std::unordered_map<std::string, Symbol *> &SemanticAnalyzer::types() const {
    return types_;
}

const std::unordered_map<std::string, Symbol *> &SemanticAnalyzer::globals() const {
    return globals_;
}

const FuncSig *SemanticAnalyzer::funcSig(const Symbol *sym) const {
    auto it = sigs_.find(sym);
    return it == sigs_.end() ? nullptr : &it->second;
}

void SemanticAnalyzer::addType(const std::string &name, const SemTypePtr &type) {
    auto sym = std::make_unique<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::BuiltinType;
    sym->type = type;
    declareType(std::move(sym));
}

void SemanticAnalyzer::addFn(const std::string &name, const std::vector<SemTypePtr> &params, const SemTypePtr &ret) {
    auto sym = std::make_unique<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::BuiltinFn;
    Symbol *raw = declareValue(std::move(sym));
    if(raw) {
        sigs_.emplace(raw, FuncSig{params, ret});
    }
}

void SemanticAnalyzer::initBuiltins() {//collect builtin members 
    addType("i32",   SemType::make(SemKind::I32));
    addType("u32",   SemType::make(SemKind::U32));
    addType("isize", SemType::make(SemKind::Isize));
    addType("usize", SemType::make(SemKind::Usize));
    addType("bool",  SemType::make(SemKind::Bool));
    addType("Box",   nullptr);
    addType("Vec",   nullptr);

    addFn("get_i32",     {}, SemType::make(SemKind::I32));
    addFn("print_i32",   {SemType::make(SemKind::I32)}, SemType::make(SemKind::Unit));
    addFn("println_i32", {SemType::make(SemKind::I32)}, SemType::make(SemKind::Unit));
}

void SemanticAnalyzer::collectItems(Crate *crate) {
    collectStructNames(crate);

    //Collect constant names first, resolve it when needed

    for(auto &it: crate->items) {
        if(it->typ == NodeType::ConstItem) {
            collectConst(static_cast<ConstItem*>(it.get()));
        }
    }//1b global consts

    for(auto &it: crate->items) {
        if(it->typ == NodeType::StructDefItem) {
            collectStructBody(static_cast<StructDefItem*>(it.get()));
        }
    }//1c struct fields

    for(auto &it: crate->items) {
        if(it->typ == NodeType::FuncDefItem) {
            collectFunc(static_cast<FuncDefItem*>(it.get()));
        }
    }//1d function signatures

    for(auto &it: crate->items) {
        if(it->typ == NodeType::ImplItem) {
            collectImpl(static_cast<ImplItem*>(it.get()));
        }
    }//1e impl

    for(auto &pair: globals_) {
        if(pair.second->kind == SymbolKind::Const) {
            evaluateConst(pair.second);
        }
    }//1f uniformly evaluate global consts
}

void SemanticAnalyzer::collectStructNames(Crate *crate) {
    for(auto &it: crate->items) {
        auto *item = it.get();
        if(item->typ == NodeType::StructDefItem) {
            auto ori = static_cast<StructDefItem*>(item);

            auto sym = std::make_unique<Symbol>();
            sym->name = ori->name;
            sym->kind = SymbolKind::Struct;
            sym->span = ori->span;
            sym->decl = ori;
            sym->type = SemType::makeStruct(ori);

            declareType(std::move(sym));
        }
    }
}

void SemanticAnalyzer::collectStructBody(StructDefItem *item) {
    StructInfo &info = structs_[item];
    info.def = item;
    std::unordered_set<std::string> vis;
    for(auto &it: item->fields) {
        auto *field = it.get();
        SemTypePtr tmp = checkType(field->type.get());
        if(!vis.insert(field->name).second) {
            error(field->span, "duplicate field: " + field->name);
        }
        info.fields.push_back({field->name, tmp, field->span});
    }
    info.derives = item->derives;
}

void SemanticAnalyzer::collectFunc(FuncDefItem *item) {
    auto sym = std::make_unique<Symbol>();
    sym->name = item->name;
    sym->kind = SymbolKind::Fn;
    sym->span = item->span;
    sym->decl = item;

    std::unordered_set<std::string> vis;

    FuncSig sig;
    for(auto &it: item->params) {
        auto *param = it.get();
        if(param->isSelf) {
            error(param->span, "Self param is only allowed in inherent impl methods");
            continue;
        }
        if(!vis.insert(param->name).second) {
            error(param->span, "duplicate param: " + param->name);
        }
        sig.params.push_back(checkType(param->type.get()));
    }
    sig.ret = item->returnType ? checkType(item->returnType.get()) : SemType::make(SemKind::Unit);

    Symbol *raw = declareValue(std::move(sym));
    if(raw) {
        sigs_[raw] = std::move(sig);
    }
}

void SemanticAnalyzer::collectConst(ConstItem *item) {
    auto sym = std::make_unique<Symbol>();
    sym->name = item->name;
    sym->type = checkType(item->type.get());
    sym->decl = item;
    sym->span = item->span;
    sym->kind = SymbolKind::Const;

    declareValue(std::move(sym));
}

void SemanticAnalyzer::collectFuncInImpl(FuncDefItem *item, StructInfo &info) {
    auto sym = std::make_unique<Symbol>();
    sym->name = item->name;
    sym->decl = item;
    sym->span = item->span;

    FuncSig sig;
    std::unordered_set<std::string> vis;
    for(size_t i = 0; i < item->params.size(); i++) {
        auto *param = item->params[i].get();
        if(param->isSelf) {
            if(i != 0) {
                error(param->span, "self must be the first parameter");
                continue;
            }
            sig.hasSelf = true;
            sig.selfByRef = param->byRef;
            sig.selfMut = param->isMut;
            sym->isSelf = true;
        }
        else {
            if(!vis.insert(param->name).second) {
                error(param->span, "duplicate param: " + param->name);
            }
            sig.params.push_back(checkType(param->type.get()));
        }
    }
    sig.ret = item->returnType ? checkType(item->returnType.get()) : SemType::make(SemKind::Unit);
    sym->kind = sig.hasSelf ? SymbolKind::Method : SymbolKind::AssocFn;

    Symbol *raw = declareMember(info, std::move(sym));
    if(raw) {
        sigs_[raw] = std::move(sig);
    }
}

void SemanticAnalyzer::collectConstInImpl(ConstItem *item, StructInfo &info) {
    auto sym = std::make_unique<Symbol>();
    sym->name = item->name;
    sym->type = checkType(item->type.get());
    sym->decl = item;
    sym->span = item->span;
    sym->kind = SymbolKind::AssocConst;

    declareMember(info, std::move(sym));
}

void SemanticAnalyzer::collectImpl(ImplItem *item) {
    const StructDefItem *selfDef = nullptr;
    /*
    Attention:
    isSelf: self
    isSelfType: Self
    */
    if(item->selfType && item->selfType->typ == NodeType::PathType) {
        auto *path = static_cast<PathType *>(item->selfType.get());
        if(path->segments.size() == 1 && !path->segments[0].isSelf && !path->segments[0].isSelfType) {
            Symbol *sym = lookupType(path->segments[0].ident);
            if(sym != nullptr && sym->kind == SymbolKind::Struct) {
                selfDef = static_cast<const StructDefItem *>(sym->decl);
            }
        }
    }
    if(selfDef == nullptr) {
        error(item->span, "inherent impl target must be a user-defined struct");
        return;
    }

    StructInfo &info = structs_[selfDef];
    info.def = selfDef;

    const StructDefItem *saved = currentImplSelf_;
    currentImplSelf_ = selfDef;

    for(auto &it: item->items) {
        if(it->typ == NodeType::ConstItem) {
            collectConstInImpl(static_cast<ConstItem*>(it.get()), info);
        }
    }
    for(auto &it: item->items) {
        if(it->typ == NodeType::FuncDefItem) {
            collectFuncInImpl(static_cast<FuncDefItem*>(it.get()), info);
        }
    }
    for(auto &pair: info.members) {
        if(pair.second->kind == SymbolKind::AssocConst) {
            evaluateConst(pair.second);
        }
    }

    currentImplSelf_ = saved;
}

const Symbol *SemanticAnalyzer::resolveConstPath(const PathExpr *path) {
    if(path == nullptr || path->segments.empty()) {
        error(path ? path->span : SourceSpan{}, "invalid constant path");
        return nullptr;
    }

    const PathSegment &first = path->segments[0];

    if(path->segments.size() == 1) {
        if(first.isSelf || first.isSelfType) {
            error(path->span, "invalid constant path");
            return nullptr;
        }
        Symbol *sym = lookupValue(first.ident);
        if(sym == nullptr) {
            error(path->span, "unresolved constant: " + first.ident);
            return nullptr;
        }
        if(sym->kind != SymbolKind::Const) {
            error(path->span, first.ident + " is not a constant");
            return nullptr;
        }
        return sym;
    }

    if(path->segments.size() == 2) {
        const StructDefItem *def = nullptr;
        if(first.isSelfType) {
            def = currentImplSelf_;
        }
        else if(first.isSelf) {
            error(path->span, "self:: path is not allowed");
            return nullptr;
        }
        else {
            Symbol *t = lookupType(first.ident);
            if(t != nullptr && t->kind == SymbolKind::Struct) {
                def = static_cast<const StructDefItem *>(t->decl);
            }
        }
        if(def == nullptr) {
            error(path->span, "constant path does not name a user-defined struct");
            return nullptr;
        }
        const PathSegment &second = path->segments[1];
        if(second.isSelf || second.isSelfType || !second.typeArgs.empty()) {
            error(path->span, "invalid constant path");
            return nullptr;
        }
        auto sit = structs_.find(def);
        if(sit == structs_.end()) {
            error(path->span, "constant path does not name an associated constant");
            return nullptr;
        }
        auto mit = sit->second.members.find(second.ident);
        if(mit == sit->second.members.end()) {
            error(path->span, "no associated item: " + second.ident);
            return nullptr;
        }
        if(mit->second->kind != SymbolKind::AssocConst) {
            error(path->span, second.ident + " is not an associated constant");
            return nullptr;
        }
        return mit->second;
    }

    error(path->span, "invalid constant path");
    return nullptr;
}

void SemanticAnalyzer::evaluateConst(Symbol *sym) {
    auto value = constEval_.evalConst(sym);
    if(!value) return;
    constValues_[sym] = *value;
    checkConstValueType(sym, *value);
}

void SemanticAnalyzer::checkConstValueType(const Symbol *sym, const ConstValue &value) {
    const SemTypePtr &type = sym->type;
    if(!type || type->isError()) return;
    if(value.kind == ConstKind::Bool) {
        if(type->kind != SemKind::Bool) {
            error(sym->span, "constant value type does not match declared type");
        }
    }
    else {
        if(!type->isInteger()) {
            error(sym->span, "constant value type does not match declared type");
        }
    }
}

SemTypePtr SemanticAnalyzer::checkType(Type *type) {
    if(type == nullptr) return SemType::makeError();
    switch(type->typ) {
        case NodeType::UnitType:
            return SemType::make(SemKind::Unit);
        case NodeType::ParenthesizedType:
            return checkType(static_cast<ParenthesizedType*>(type)->inner.get());
        case NodeType::ArrayType: {
            auto *arr = static_cast<ArrayType*>(type);
            auto len = constEval_.eval(arr->len.get());
            if(!len || len->kind != ConstKind::Int) {
                error(arr->span, "array length is not an integer const");
                return SemType::makeError();
            }
            return SemType::makeArray(checkType(arr->elem.get()), static_cast<std::size_t>(len->val.val));
        }
        case NodeType::RefType: {
            auto *ref = static_cast<RefType*>(type);
            return SemType::makeRef(checkType(ref->referent.get()), ref->isMut);
        }
        case NodeType::PathType: {
            return checkPathType(static_cast<PathType*>(type));
        }
        default: break;
    }

    error(type->span, "unsupported type");
    return SemType::makeError();
}

SemTypePtr SemanticAnalyzer::checkPathType(PathType *type) {
    (void)type;
    return SemType::makeError();
}

Symbol *SemanticAnalyzer::declareType(std::unique_ptr<Symbol> sym) {
    Symbol *raw = sym.get();
    auto it = types_.find(raw->name);
    if(it != types_.end()) {
        error(raw->span, "duplicate type definition: " + raw->name);
        return it->second;
    }
    types_.emplace(raw->name, raw);
    ownedSymbols_.push_back(std::move(sym));
    return raw;
}

Symbol *SemanticAnalyzer::declareValue(std::unique_ptr<Symbol> sym) {//including value, fn, etc.
    Symbol *raw = sym.get();
    auto it = globals_.find(raw->name);
    if(it != globals_.end()) {
        error(raw->span, "duplicate value definition: " + raw->name);
        return it->second;
    }
    globals_.emplace(raw->name, raw);
    ownedSymbols_.push_back(std::move(sym));
    return raw;
}

Symbol *SemanticAnalyzer::declareMember(StructInfo &info, std::unique_ptr<Symbol> sym) {
    Symbol *raw = sym.get();
    auto it = info.members.find(raw->name);
    if(it != info.members.end()) {
        error(raw->span, "duplicate associated item: " + raw->name);
        return it->second;
    }
    info.members.emplace(raw->name, raw);
    ownedSymbols_.push_back(std::move(sym));
    return raw;
}

void SemanticAnalyzer::error(SourceSpan span, std::string message) {
    diags.push_back({span, std::move(message)});
}

void SemanticAnalyzer::checkFunc(FuncDefItem *item) {
    (void)item;
}

void SemanticAnalyzer::checkStmt(Stmt *stmt, Scope &scope) {
    (void)stmt;
    (void)scope;
}

SemTypePtr SemanticAnalyzer::checkExpr(Expr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expr;
    (void)scope;
    (void)expected;
    return SemType::makeError();
}
