#include "analyzer.h"
#include "AST/type.h"
#include "semantic/symbol.h"
#include <set>
#include <utility>
#include <unordered_map>
#include <unordered_set>

bool SemanticAnalyzer::analyze(Crate *crate) {
    if(crate == nullptr) return false;
    initBuiltins();
    constEval.setAnalyzer(this);
    collectItems(crate);
    checkEntry();//Phase 1: collection

    checkBody(crate);//Phase 2: concrete semantic
    return !hasError();
}

bool SemanticAnalyzer::hasError() const {
    return !diags.empty();
}

const std::vector<Diagnostic> &SemanticAnalyzer::diagnostics() const {
    return diags;
}

Symbol *SemanticAnalyzer::lookupType(const std::string &name) const {
    auto it = types.find(name);
    return it == types.end() ? nullptr : it->second;
}

Symbol *SemanticAnalyzer::lookupValue(const std::string &name) const {
    auto it = globals.find(name);
    return it == globals.end() ? nullptr : it->second;
}

const StructInfo *SemanticAnalyzer::structInfo(const StructDefItem *def) const {
    auto it = structs.find(def);
    return it == structs.end() ? nullptr : &it->second;
}

const std::unordered_map<std::string, Symbol *> &SemanticAnalyzer::typeTable() const {
    return types;
}

const std::unordered_map<std::string, Symbol *> &SemanticAnalyzer::globalTable() const {
    return globals;
}

const FuncSig *SemanticAnalyzer::funcSig(const Symbol *sym) const {
    auto it = sigs.find(sym);
    return it == sigs.end() ? nullptr : &it->second;
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
        sigs.emplace(raw, FuncSig{params, ret});
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

    //Phase 1

    for(auto &it: crate->items) {
        if(it->typ == NodeType::ConstItem) {
            collectConst(static_cast<ConstItem*>(it.get()));
        }
    }

    for(auto &it: crate->items) {
        if(it->typ == NodeType::StructDefItem) {
            collectStructBody(static_cast<StructDefItem*>(it.get()));
        }
    }

    checkLayoutCycles();
    checkDerives();

    for(auto &it: crate->items) {
        if(it->typ == NodeType::FuncDefItem) {
            collectFunc(static_cast<FuncDefItem*>(it.get()));
        }
    }

    for(auto &it: crate->items) {
        if(it->typ == NodeType::ImplItem) {
            collectImpl(static_cast<ImplItem*>(it.get()));
        }
    }

    for(auto &pair: globals) {
        if(pair.second->kind == SymbolKind::Const) {
            evaluateConst(pair.second);
        }
    }//calculate constants
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
    StructInfo &info = structs[item];
    info.def = item;

    const StructDefItem *saved = currentImplSelf;
    currentImplSelf = item;

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

    currentImplSelf = saved;
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
        sigs[raw] = std::move(sig);
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
        sigs[raw] = std::move(sig);
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
    if(item->selfType) {
        SemTypePtr target = checkType(item->selfType.get());
        if(target && target->kind == SemKind::Struct) {
            selfDef = target->structDef;
        }
    }
    if(selfDef == nullptr) {
        error(item->span, "inherent impl target must be a user-defined struct");
        return;
    }

    StructInfo &info = structs[selfDef];
    info.def = selfDef;
    implTargets[item] = selfDef;

    const StructDefItem *saved = currentImplSelf;
    currentImplSelf = selfDef;

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

    currentImplSelf = saved;
}

const Symbol *SemanticAnalyzer::resolveConstPath(const PathExpr *path, bool report) {//const def like xxx = Self::/A::
    SourceSpan span = path ? path->span : SourceSpan{};
    auto fail = [&](std::string message) -> const Symbol * {
        if(report) error(span, std::move(message));
        return nullptr;
    };

    if(path == nullptr || path->segments.empty()) {
        return fail("invalid constant path");
    }

    const PathSegment &first = path->segments[0];

    if(path->segments.size() == 1) {
        if(first.isSelf || first.isSelfType) {
            return fail("invalid constant path");
        }
        Symbol *sym = lookupValue(first.ident);
        if(sym == nullptr) {
            return fail("unresolved constant: " + first.ident);
        }
        if(sym->kind != SymbolKind::Const) {
            return fail(first.ident + " is not a constant");
        }
        return sym;
    }

    if(path->segments.size() == 2) {
        const StructDefItem *def = nullptr;
        if(first.isSelfType) {
            def = currentImplSelf;
        }
        else if(first.isSelf) {
            return fail("self:: path is not allowed");
        }
        else {
            Symbol *t = lookupType(first.ident);
            if(t != nullptr && t->kind == SymbolKind::Struct) {
                def = static_cast<const StructDefItem *>(t->decl);
            }
        }
        if(def == nullptr) {
            return fail("constant path does not name a user-defined struct");
        }
        const PathSegment &second = path->segments[1];
        if(second.isSelf || second.isSelfType || !second.typeArgs.empty()) {
            return fail("invalid constant path");
        }
        auto sit = structs.find(def);
        if(sit == structs.end()) {
            return fail("constant path does not name an associated constant");
        }
        auto mit = sit->second.members.find(second.ident);
        if(mit == sit->second.members.end()) {
            return fail("no associated item: " + second.ident);
        }
        if(mit->second->kind != SymbolKind::AssocConst) {
            return fail(second.ident + " is not an associated constant");
        }
        return mit->second;
    }

    return fail("invalid constant path");
}

void SemanticAnalyzer::evaluateConst(Symbol *sym) {
    auto value = constEval.evalConst(sym);
    if(!value) return;
    constValues[sym] = *value;
    checkConstValueType(sym, *value);
}

void SemanticAnalyzer::checkConstValueType(const Symbol *sym, const ConstValue &value) {
    const SemTypePtr &type = sym->type;
    if(!type || type->isError()) return;
    if(value.kind == ConstKind::Bool) {
        if(type->kind != SemKind::Bool) {
            error(sym->span, "constant value type does not match declared type");
        }
        return;
    }
    if(!type->isInteger()) {
        error(sym->span, "constant value type does not match declared type");
        return;
    }
    switch(value.val.suffix) {
        case IntSuffix::Infer: break;
        case IntSuffix::I32:   if(type->kind != SemKind::I32)   error(sym->span, "constant initializer type does not match declared type"); break;
        case IntSuffix::U32:   if(type->kind != SemKind::U32)   error(sym->span, "constant initializer type does not match declared type"); break;
        case IntSuffix::Isize: if(type->kind != SemKind::Isize) error(sym->span, "constant initializer type does not match declared type"); break;
        case IntSuffix::Usize: if(type->kind != SemKind::Usize) error(sym->span, "constant initializer type does not match declared type"); break;
    }
}

bool SemanticAnalyzer::constExprIsUsize(const Expr *expr) {
    if(expr == nullptr) return false;
    switch(expr->typ) {        case NodeType::IntExpr: {
            IntSuffix suffix = static_cast<const IntExpr*>(expr)->val.suffix;
            return suffix == IntSuffix::Infer || suffix == IntSuffix::Usize;
        }
        case NodeType::GroupExpr:
            return constExprIsUsize(static_cast<const GroupExpr*>(expr)->inner.get());
        case NodeType::UnaryExpr:
            return false;
        case NodeType::PathExpr: {
            const Symbol *sym = resolveConstPath(static_cast<const PathExpr*>(expr), false);
            if(sym == nullptr || !sym->type || sym->type->isError()) return true;
            return sym->type->kind == SemKind::Usize;
        }
        default:
            return true;
    }
}

void SemanticAnalyzer::checkEntry() {
    Symbol *mainSym = lookupValue("main");
    if(mainSym == nullptr || mainSym->kind != SymbolKind::Fn) {
        error(SourceSpan{}, "an executable requires a top-level fn main");
        return;
    }
    const auto *decl = static_cast<const FuncDefItem*>(mainSym->decl);
    if(decl != nullptr && !decl->genericParams.empty()) {
        error(mainSym->span, "main cannot have generic parameters");
    }
    const FuncSig *sig = funcSig(mainSym);
    if(sig == nullptr) return;
    if(!sig->params.empty()) {
        error(mainSym->span, "main cannot have value parameters");
    }
    if(sig->ret && sig->ret->kind != SemKind::Unit) {
        error(mainSym->span, "main must return unit");
    }
}

void SemanticAnalyzer::collectInlineStructs(const SemTypePtr &type, std::vector<const StructDefItem *> &out) {
    if(!type) return;
    switch(type->kind) {
        case SemKind::Struct:
            if(type->structDef) out.push_back(type->structDef);
            break;
        case SemKind::Array:
            collectInlineStructs(type->elem, out);
            break;
        default:
            break;
    }
}

bool SemanticAnalyzer::layoutDfs(const StructDefItem *def, std::unordered_map<const StructDefItem *, int> &color) {
    //color: 0: unvisited, 1: processing, 2:done
    color[def] = 1;
    const StructInfo *info = structInfo(def);
    if(info) {
        for(auto &field : info->fields) {
            std::vector<const StructDefItem *> children;
            collectInlineStructs(field.type, children);
            for(const StructDefItem *child : children) {
                auto it = color.find(child);
                int c = (it == color.end()) ? 0 : it->second;
                if(c == 1) {
                    error(def->span, "recursive inline layout");
                    return true;
                }
                if(c == 0 && layoutDfs(child, color)) return true;
            }
        }
    }
    color[def] = 2;
    return false;
}

void SemanticAnalyzer::checkLayoutCycles() {
    std::unordered_map<const StructDefItem *, int> color;
    for(auto &kv : structs) {
        auto it = color.find(kv.first);
        if(it == color.end() || it->second == 0) {
            if(layoutDfs(kv.first, color)) return;
        }
    }
}

bool SemanticAnalyzer::structHasDerive(const StructDefItem *def, Derive d) const {
    auto it = structs.find(def);
    if(it == structs.end()) return false;
    for(Derive x : it->second.derives) {
        if(x == d) return true;
    }
    return false;
}

bool SemanticAnalyzer::typeSupportsDerive(const SemTypePtr &type, Derive d,
        std::set<std::pair<const StructDefItem *, Derive>> &inprogress) {
    if(!type || type->isError()) return true;
    switch(type->kind) {
        case SemKind::I32: 
        case SemKind::U32: 
        case SemKind::Isize:
        case SemKind::Usize: 
        case SemKind::Bool: 
        case SemKind::Unit: 
        case SemKind::Never:
            return true;
        case SemKind::Ref: {
            if(d == Derive::Copy || d == Derive::Clone) {
                return !type->isMut;
            }
            return typeSupportsDerive(type->inner, d, inprogress);
        }
        case SemKind::Array:
            return typeSupportsDerive(type->elem, d, inprogress);
        case SemKind::Box:
        case SemKind::Vec: {
            if(d == Derive::Copy) {
                return false;
            }
            return typeSupportsDerive(type->inner, d, inprogress);
        }
        case SemKind::Struct: {
            const StructDefItem *def = type->structDef;
            if(!structHasDerive(def, d)) return false;
            auto key = std::make_pair(def, d);
            if(inprogress.count(key)) return true;
            inprogress.insert(key);
            bool ok = true;
            const StructInfo *info = structInfo(def);
            if(info) {
                for(auto &field : info->fields) {
                    if(!typeSupportsDerive(field.type, d, inprogress)) { ok = false; break; }
                }
            }
            inprogress.erase(key);
            return ok;
        }
        default:
            return true;
    }
}

void SemanticAnalyzer::checkDerives() {
    for(auto &kv : structs) {
        const StructDefItem *def = kv.first;
        StructInfo &info = kv.second;

        std::unordered_set<int> seen;
        bool hasCopy = false, hasClone = false, hasEq = false, hasPartialEq = false;
        for(Derive d : info.derives) {
            if(!seen.insert(static_cast<int>(d)).second) {
                error(def->span, "repeated derive entry");
            }
            if(d == Derive::Copy) hasCopy = true;
            else if(d == Derive::Clone) hasClone = true;
            else if(d == Derive::Eq) hasEq = true;
            else if(d == Derive::PartialEq) hasPartialEq = true;
        }
        if(hasCopy && !hasClone) error(def->span, "Copy requires Clone");
        if(hasEq && !hasPartialEq) error(def->span, "Eq requires PartialEq");

        std::set<std::pair<const StructDefItem *, Derive>> inprogress;
        for(Derive d : info.derives) {
            for(auto &field : info.fields) {
                if(!typeSupportsDerive(field.type, d, inprogress)) {
                    error(def->span, "a field does not support the requested derive");
                    break;
                }
            }
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
            auto len = constEval.eval(arr->len.get());
            if(!len || len->kind != ConstKind::Int) {
                error(arr->span, "array length is not an integer const");
                return SemType::makeError();
            }
            if(!constExprIsUsize(arr->len.get())) {
                error(arr->span, "array length must have usize type");
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
    if(type->segments.empty()) {
        error(type->span, "empty type path");
        return SemType::makeError();
    }
    if(type->segments.size() > 1) {//multi type path is not allowed in Rx according to spec
        error(type->span, "multi type path");
        return SemType::makeError();
    }
    const PathSegment &seg = type->segments[0];
    if(seg.isSelf) {//self
        error(type->span, "self is not a type");
        return SemType::makeError();
    }
    if(seg.isSelfType) {//Self
        if(!seg.typeArgs.empty()) {//Rx does not support
            error(type->span, "Self shouldn't include type args");
            return SemType::makeError();
        }
        if(currentImplSelf == nullptr) {
            error(type->span, "Self outside a struct or an impl");
            return SemType::makeError();
        }
        return SemType::makeStruct(currentImplSelf);
    }
    Symbol *sym = lookupType(seg.ident);

    if(sym == nullptr) {
        error(type->span, "unknown type");
        return SemType::makeError();
    }

    if(sym->kind == SymbolKind::BuiltinType && sym->type == nullptr) {//Vec and Box
        if(seg.typeArgs.size() != 1) {
            error(type->span, "Box/Vec should include exactly one type arg");
            return SemType::makeError();
        }
        SemKind nw = seg.ident == "Vec" ? SemKind::Vec : SemKind::Box;
        return SemType::makeContainer(nw, checkType(seg.typeArgs[0].get()));
    }

    if(!seg.typeArgs.empty()) {
        error(type->span, seg.ident + " shouldn't include type args");
        return SemType::makeError();
    }
    return sym->type;
}

Symbol *SemanticAnalyzer::declareType(std::unique_ptr<Symbol> sym) {
    Symbol *raw = sym.get();
    auto it = types.find(raw->name);
    if(it != types.end()) {
        error(raw->span, "duplicate type definition: " + raw->name);
        return it->second;
    }
    types.emplace(raw->name, raw);
    ownedSymbols.push_back(std::move(sym));
    return raw;
}

Symbol *SemanticAnalyzer::declareValue(std::unique_ptr<Symbol> sym) {//including value, fn, etc.
    Symbol *raw = sym.get();
    auto it = globals.find(raw->name);
    if(it != globals.end()) {
        error(raw->span, "duplicate value definition: " + raw->name);
        return it->second;
    }
    globals.emplace(raw->name, raw);
    ownedSymbols.push_back(std::move(sym));
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
    ownedSymbols.push_back(std::move(sym));
    return raw;
}

void SemanticAnalyzer::error(SourceSpan span, std::string message) {
    diags.push_back({span, std::move(message)});
}

void SemanticAnalyzer::checkBody(Crate *crate) {
    //Phase 2
    //register funcs
    for(auto &it: crate->items) {
        switch(it->typ) {
            case NodeType::FuncDefItem: {
                checkFunc(static_cast<FuncDefItem*>(it.get()));
                break;
            }
            case NodeType::ImplItem: {
                checkImpl(static_cast<ImplItem*>(it.get()));
                break;
            }
            default: break;
        }
    }
    //TODO
}

void SemanticAnalyzer::checkFunc(FuncDefItem *item) {
    Symbol *funcSym = nullptr;
    if(currentImplSelf != nullptr) {
        auto &members = structs[currentImplSelf].members;
        auto it = members.find(item->name);
        if(it != members.end()) funcSym = it->second;
    }
    else {
        funcSym = lookupValue(item->name);
    }
    const FuncSig *sig = funcSym ? funcSig(funcSym) : nullptr;

    Scope scope(nullptr);
    size_t pos = 0;//index into sig->params (not including self)
    for(auto &it: item->params) {
        auto *param = it.get();
        auto sym = std::make_unique<Symbol>();
        sym->kind = SymbolKind::Param;
        sym->span = param->span;
        sym->decl = param;
        sym->isPlace = true;
        sym->isMut = param->isMut;
        if(param->isSelf) {
            if(currentImplSelf == nullptr) continue;//top-level self: already reported in Phase 1
            sym->isSelf = true;
            sym->name = "self";
            SemTypePtr base = SemType::makeStruct(currentImplSelf);
            sym->type = param->byRef ? SemType::makeRef(base, param->isMut) : base;
        }
        else {
            sym->name = param->name;
            if(sig != nullptr && pos < sig->params.size()) {
                sym->type = sig->params[pos];
            }
            else {
                sym->type = checkType(param->type.get());
            }
            pos++;
        }
        scope.define(std::move(sym));
    }

    if(item->body == nullptr) return;
    for(auto &stmt: item->body->statements) {
        checkStmt(stmt.get(), scope);
    }
    if(item->body->tail) {
        const Expr *tail = item->body->tail.get();
        SemTypePtr result = checkExpr(item->body->tail.get(), scope, sig ? sig->ret : nullptr);
        if(sig && sig->ret && !sig->ret->isError() && result && !result->isError() &&
           !result->isNever() && !canCoerce(result, sig->ret)) {
            error(tail->span, "function body type does not match return type");
        }
    }
    else {//by return expr
        //TODO
    }
}

void SemanticAnalyzer::checkImpl(ImplItem *item) {
    const StructDefItem *def = nullptr;
    auto it = implTargets.find(item);
    if(it != implTargets.end()) def = it->second;
    if(def == nullptr) return;//invalid impl target: already reported in Phase 1

    const StructDefItem *saved = currentImplSelf;
    currentImplSelf = def;
    for(auto &m: item->items) {
        if(m->typ == NodeType::FuncDefItem) {
            checkFunc(static_cast<FuncDefItem*>(m.get()));
        }
    }
    currentImplSelf = saved;
}

void SemanticAnalyzer::checkStmt(Stmt *stmt, Scope &scope) {
    switch(stmt->typ) {
        case NodeType::LetStmt: {
            auto *st = static_cast<LetStmt*>(stmt);
            auto assignType = st->type ? checkType(st->type.get()) : nullptr;
            auto initType = checkExpr(st->init.get(), scope, assignType);
            if(assignType && !assignType->isError() && initType && !initType->isError() && 
               !initType->isNever() && !canCoerce(initType, assignType)) {
                error(st->span, "initializer type does not match the expected type");
            }
            auto sym = std::make_unique<Symbol>();
            sym->name = st->name;
            sym->kind = SymbolKind::Local;
            sym->span = st->span;
            sym->decl = st;
            sym->isMut = st->isMut;
            sym->isPlace = true;
            sym->type = assignType ? assignType
                                   : (initType ? initType : SemType::makeError());
            scope.define(std::move(sym));
            break;
        }
        case NodeType::ExprStmt: {
            auto *st = static_cast<ExprStmt*>(stmt);
            auto unit = SemType::makeUnit();
            auto tmp = \
                checkExpr(st->expr.get(), scope, st->hasSemi ? nullptr : unit);
            if(!st->hasSemi && tmp && !tmp->isError() && !tmp->isNever() && !canCoerce(tmp, unit)) {
                error(st->span, "block expr without semi must be unit type");
            }
            break;
        }
        case NodeType::NullStmt:
        default: break;
    }
}

SemTypePtr SemanticAnalyzer::checkExpr(Expr *expr, Scope &scope, const SemTypePtr &expected) {
    switch(expr->typ) {
        case NodeType::IntExpr: {
            auto *tmp = static_cast<IntExpr*>(expr);
            SemKind kind;
            switch(tmp->val.suffix) {
                case IntSuffix::I32: {
                    kind = SemKind::I32;
                    break;
                }
                case IntSuffix::U32: {
                    kind = SemKind::U32;
                    break;
                }
                case IntSuffix::Isize: {
                    kind = SemKind::Isize;
                    break;
                }
                case IntSuffix::Usize: {
                    kind = SemKind::Usize;
                    break;
                }
                case IntSuffix::Infer: {
                    kind = (expected && expected->isInteger()) ? expected->kind : SemKind::I32;
                    break;
                }
            }
            auto ret = SemType::make(kind);
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(tmp->span, "type mismatch");
            }
            return ret;
        }
        case NodeType::BoolExpr: {
            auto ret = SemType::make(SemKind::Bool);
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(expr->span, "type mismatch");
            }
            return ret;
        }
        case NodeType::UnaryExpr: {
            auto *tmp = static_cast<UnaryExpr*>(expr);
            switch(tmp->op) {
                
            }
        }
        case NodeType::BinaryExpr: {

        }
        case NodeType::PathExpr: {

        }
        case NodeType::AssignExpr: {

        }
        case NodeType::ArrayExpr: {

        }
        case NodeType::BlockExpr: {

        }
        case NodeType::BreakExpr: {

        }
        case NodeType::CallExpr: {

        }
        case NodeType::CastExpr: {

        }
        default: break;
    }
    return SemType::makeError();
}
