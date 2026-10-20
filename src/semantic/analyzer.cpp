#include "analyzer.h"
#include "AST/AstNode.h"
#include "AST/type.h"
#include "semantic/symbol.h"
#include <set>
#include <utility>
#include <unordered_map>
#include <unordered_set>

static bool placeDerefMut(const SemTypePtr &type, bool baseMut);

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
    auto it = result.structs.find(def);
    return it == result.structs.end() ? nullptr : &it->second;
}

const std::unordered_map<std::string, Symbol *> &SemanticAnalyzer::typeTable() const {
    return types;
}

const std::unordered_map<std::string, Symbol *> &SemanticAnalyzer::globalTable() const {
    return globals;
}

const SemaResult &SemanticAnalyzer::semaResult() const {
    return result;
}

const FuncSig *SemanticAnalyzer::funcSig(const Symbol *sym) const {
    auto it = result.sigs.find(sym);
    return it == result.sigs.end() ? nullptr : &it->second;
}

void SemanticAnalyzer::addType(const std::string &name, const SemTypePtr &type) {
    auto sym = std::make_unique<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::BuiltinType;
    sym->type = type;
    declareType(std::move(sym));
}

void SemanticAnalyzer::addFn(const std::string &name, const std::vector<SemTypePtr> &params, const SemTypePtr &ret,
                             BuiltinOp op) {
    auto sym = std::make_unique<Symbol>();
    sym->name = name;
    sym->kind = SymbolKind::BuiltinFn;
    Symbol *raw = declareValue(std::move(sym));
    if(raw) {
        result.sigs.emplace(raw, FuncSig{params, ret});
        if(op != BuiltinOp::None) {
            result.builtinOps[raw] = op;
        }
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

    addFn("get_i32",     {}, SemType::make(SemKind::I32), BuiltinOp::GetI32);
    addFn("print_i32",   {SemType::make(SemKind::I32)}, SemType::make(SemKind::Unit), BuiltinOp::PrintI32);
    addFn("println_i32", {SemType::make(SemKind::I32)}, SemType::make(SemKind::Unit), BuiltinOp::PrintlnI32);

    initContainerMembers();
}

void SemanticAnalyzer::initContainerMembers() {
    containerMembers["Box"]["new"]      = {false, false, false, {ContainerArg::T},     ContainerRet::BoxT,   BuiltinOp::BoxNew};
    containerMembers["Box"]["clone"]    = {true,  true,  false, {},                    ContainerRet::BoxT,   BuiltinOp::BoxClone};
    containerMembers["Vec"]["new"]      = {false, false, false, {},                    ContainerRet::VecT,   BuiltinOp::VecNew};
    containerMembers["Vec"]["clone"]    = {true,  true,  false, {},                    ContainerRet::VecT,   BuiltinOp::VecClone};
    containerMembers["Vec"]["len"]      = {true,  true,  false, {},                    ContainerRet::Usize,  BuiltinOp::VecLen};
    containerMembers["Vec"]["is_empty"] = {true,  true,  false, {},                    ContainerRet::Bool,   BuiltinOp::VecIsEmpty};
    containerMembers["Vec"]["push"]     = {true,  true,  true,  {ContainerArg::T},     ContainerRet::Unit,   BuiltinOp::VecPush};
    containerMembers["Vec"]["remove"]   = {true,  true,  true,  {ContainerArg::Usize}, ContainerRet::T,      BuiltinOp::VecRemove};
}

static SemTypePtr containerArgType(ContainerArg arg, const SemTypePtr &T) {
    if(arg == ContainerArg::Usize) return SemType::make(SemKind::Usize);
    return T;
}

static SemTypePtr containerRetType(ContainerRet ret, const SemTypePtr &T) {
    switch(ret) {
        case ContainerRet::T:     return T;
        case ContainerRet::BoxT:  return SemType::makeContainer(SemKind::Box, T);
        case ContainerRet::VecT:  return SemType::makeContainer(SemKind::Vec, T);
        case ContainerRet::Usize: return SemType::make(SemKind::Usize);
        case ContainerRet::Bool:  return SemType::make(SemKind::Bool);
        case ContainerRet::Unit:  return SemType::makeUnit();
    }
    return SemType::makeUnit();
}

Symbol *SemanticAnalyzer::getContainerMember(const std::string &container, const std::string &name,
                                                     const ContainerMember &member, const SemTypePtr &T,
                                                     SourceSpan span) {
    SemKind kind = container == "Box" ? SemKind::Box : SemKind::Vec;
    auto sym = std::make_unique<Symbol>();
    sym->name = name;
    sym->kind = member.hasSelf ? SymbolKind::Method : SymbolKind::AssocFn;
    sym->span = span;
    if(member.hasSelf) {
        sym->type = SemType::makeContainer(kind, T);
    }
    FuncSig sig;
    sig.hasSelf = member.hasSelf;
    sig.selfByRef = member.selfByRef;
    sig.selfMut = member.selfMut;
    for(ContainerArg arg : member.params) {
        sig.params.push_back(containerArgType(arg, T));
    }
    sig.ret = containerRetType(member.ret, T);
    Symbol *raw = sym.get();
    result.ownedSymbols.push_back(std::move(sym));
    result.sigs[raw] = std::move(sig);
    if(member.op != BuiltinOp::None) {
        result.builtinOps[raw] = member.op;
    }
    return raw;
}

Symbol *SemanticAnalyzer::getStructClone(const StructDefItem *def, SourceSpan span) {
    auto sym = std::make_unique<Symbol>();
    sym->name = "clone";
    sym->kind = SymbolKind::Method;
    sym->span = span;
    sym->type = SemType::makeStruct(def);
    FuncSig sig;
    sig.hasSelf = true;
    sig.selfByRef = true;
    sig.ret = SemType::makeStruct(def);
    Symbol *raw = sym.get();
    result.ownedSymbols.push_back(std::move(sym));
    result.sigs[raw] = std::move(sig);
    result.builtinOps[raw] = BuiltinOp::Clone;
    return raw;
}

Symbol *SemanticAnalyzer::getBuiltinClone(const SemTypePtr &type, SourceSpan span) {
    auto sym = std::make_unique<Symbol>();
    sym->name = "clone";
    sym->kind = SymbolKind::Method;
    sym->span = span;
    sym->type = type;
    FuncSig sig;
    sig.hasSelf = true;
    sig.selfByRef = true;
    sig.ret = type;
    Symbol *raw = sym.get();
    result.ownedSymbols.push_back(std::move(sym));
    result.sigs[raw] = std::move(sig);
    result.builtinOps[raw] = BuiltinOp::Clone;
    return raw;
}

Symbol *SemanticAnalyzer::getArrayLen(const SemTypePtr &type, SourceSpan span) {
    auto sym = std::make_unique<Symbol>();
    sym->name = "len";
    sym->kind = SymbolKind::Method;
    sym->span = span;
    sym->type = type;
    FuncSig sig;
    sig.hasSelf = true;
    sig.selfByRef = true;
    sig.ret = SemType::make(SemKind::Usize);
    Symbol *raw = sym.get();
    result.ownedSymbols.push_back(std::move(sym));
    result.sigs[raw] = std::move(sig);
    result.builtinOps[raw] = BuiltinOp::ArrayLen;
    return raw;
}

Symbol *SemanticAnalyzer::findMethod(const SemTypePtr &candidate, const std::string &name, SourceSpan span) {
    if(!candidate) return nullptr;
    SemTypePtr type = candidate;
    int form = 0;
    if(type->kind == SemKind::Ref) {
        form = type->isMut ? 2 : 1;
        type = type->inner;
    }
    std::set<std::pair<const StructDefItem *, Derive>> vis;
    switch(type->kind) {
        case SemKind::Struct: {
            auto it = result.structs.find(type->structDef);
            if(it == result.structs.end()) return nullptr;
            auto mit = it->second.members.find(name);
            if(mit == it->second.members.end()) return nullptr;
            Symbol *mem = mit->second;
            if(mem->kind != SymbolKind::Method) return nullptr;
            const FuncSig *sig = funcSig(mem);
            if(sig == nullptr) return nullptr;
            if(form == 0 && sig->selfByRef) return nullptr;
            if(form == 1 && (!sig->selfByRef || sig->selfMut)) return nullptr;
            if(form == 2 && (!sig->selfByRef || !sig->selfMut)) return nullptr;
            return mem;
        }
        case SemKind::Box:
        case SemKind::Vec: {
            if(form == 0) return nullptr;
            std::string container = type->kind == SemKind::Box ? "Box" : "Vec";
            auto cit = containerMembers.find(container);
            if(cit == containerMembers.end()) return nullptr;
            auto mit = cit->second.find(name);
            if(mit == cit->second.end()) return nullptr;
            const ContainerMember &mem = mit->second;
            if(!mem.hasSelf || mem.selfMut != (form == 2)) return nullptr;
            if(name == "clone" && !typeSupportsDerive(type->inner, Derive::Clone, vis)) return nullptr;
            return getContainerMember(container, name, mem, type->inner, span);
        }
        case SemKind::Array: {
            if(form != 1) return nullptr;
            if(name == "len") return getArrayLen(type, span);
            if(name == "clone" && typeSupportsDerive(type->elem, Derive::Clone, vis)) return getBuiltinClone(type, span);
            return nullptr;
        }
        case SemKind::I32:
        case SemKind::U32:
        case SemKind::Isize:
        case SemKind::Usize:
        case SemKind::Bool: {
            if(form == 1 && name == "clone") return getBuiltinClone(type, span);
            return nullptr;
        }
        case SemKind::Ref: {
            if(form == 1 && name == "clone" && !type->isMut) return getBuiltinClone(type, span);
            return nullptr;
        }
        default:
            return nullptr;
    }
}

void SemanticAnalyzer::collectGeneratedClones() {
    for(auto &pair: result.structs) {
        StructInfo &info = pair.second;
        if(!structHasDerive(pair.first, Derive::Clone)) continue;
        if(info.members.find("clone") != info.members.end()) continue;
        info.members["clone"] = getStructClone(pair.first, pair.first->span);
    }
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

    collectGeneratedClones();

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
    StructInfo &info = result.structs[item];
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
        info.fieldIndex.emplace(field->name, info.fields.size());
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
        result.sigs[raw] = std::move(sig);
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
    if(sig.hasSelf) {
        sym->type = SemType::makeStruct(info.def);
    }

    Symbol *raw = declareMember(info, std::move(sym));
    if(raw) {
        result.sigs[raw] = std::move(sig);
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

    StructInfo &info = result.structs[selfDef];
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
        auto sit = result.structs.find(def);
        if(sit == result.structs.end()) {
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
    result.constValues[sym] = *value;
    checkConstValueType(sym, *value);
}

void SemanticAnalyzer::checkConstValueType(const Symbol *sym, const ConstValue &value) {
    const SemTypePtr &declared = sym->type;
    if(!declared || declared->isError()) return;
    if(value.kind == ConstKind::Bool) {
        if(declared->kind != SemKind::Bool) {
            error(sym->span, "constant value type does not match declared type");
        }
        return;
    }
    if(!declared->isInteger()) {
        error(sym->span, "constant value type does not match declared type");
        return;
    }
    const SemTypePtr &vt = value.type;
    if(!vt || vt->isError()) return;//unknown/infer: adopts declared type
    if(!canCoerce(vt, declared)) {
        error(sym->span, "constant initializer type does not match declared type");
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
        StructInfo &out = result.structs[def];
        out.fieldOffsets.assign(out.fields.size(), 0);
        size_t offset = 0, align = 1;
        for(size_t i = 0; i < out.fields.size(); i++) {
            TypeLayout field = layoutOf(out.fields[i].type);
            offset = alignTo(offset, field.align);
            out.fieldOffsets[i] = offset;
            offset += field.size;
            if(field.align > align) align = field.align;//maxsize of struct members
        }
        out.layout.align = align;
        out.layout.size = alignTo(offset, align);
    }
    color[def] = 2;
    return false;
}

void SemanticAnalyzer::checkLayoutCycles() {
    std::unordered_map<const StructDefItem *, int> color;
    for(auto &kv : result.structs) {
        auto it = color.find(kv.first);
        if(it == color.end() || it->second == 0) {
            if(layoutDfs(kv.first, color)) return;
        }
    }
}

bool SemanticAnalyzer::structHasDerive(const StructDefItem *def, Derive d) const {
    auto it = result.structs.find(def);
    if(it == result.structs.end()) return false;
    for(Derive x : it->second.derives) {
        if(x == d) return true;
    }
    return false;
}

bool SemanticAnalyzer::typeSupportsDerive(const SemTypePtr &type, Derive d,
        std::set<std::pair<const StructDefItem *, Derive>> &vis) {
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
            return typeSupportsDerive(type->inner, d, vis);
        }
        case SemKind::Array:
            return typeSupportsDerive(type->elem, d, vis);
        case SemKind::Box:
        case SemKind::Vec: {
            if(d == Derive::Copy) {
                return false;
            }
            return typeSupportsDerive(type->inner, d, vis);
        }
        case SemKind::Struct: {
            const StructDefItem *def = type->structDef;
            if(!structHasDerive(def, d)) return false;
            auto key = std::make_pair(def, d);
            if(vis.count(key)) return true;
            vis.insert(key);
            bool ok = true;
            const StructInfo *info = structInfo(def);
            if(info) {
                for(auto &field : info->fields) {
                    if(!typeSupportsDerive(field.type, d, vis)) { ok = false; break; }
                }
            }
            vis.erase(key);
            return ok;
        }
        default:
            return true;
    }
}

void SemanticAnalyzer::checkDerives() {
    for(auto &kv : result.structs) {
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

        std::set<std::pair<const StructDefItem *, Derive>> vis;
        for(Derive d : info.derives) {
            for(auto &field : info.fields) {
                if(!typeSupportsDerive(field.type, d, vis)) {
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
    result.ownedSymbols.push_back(std::move(sym));
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
    result.ownedSymbols.push_back(std::move(sym));
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
    result.ownedSymbols.push_back(std::move(sym));
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
        auto &members = result.structs[currentImplSelf].members;
        auto it = members.find(item->name);
        if(it != members.end()) funcSym = it->second;
    }
    else {
        funcSym = lookupValue(item->name);
    }
    const FuncSig *sig = funcSym ? funcSig(funcSym) : nullptr;

    Scope scope(result.ownedSymbols);
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
    SemTypePtr savedRet = currentFuncRet;
    currentFuncRet = sig ? sig->ret : nullptr;
    bool bodyError = false, bodyNever = false;
    for(auto &stmt: item->body->statements) {
        SemTypePtr t = checkStmt(stmt.get(), scope);
        if(!t || t->isError()) bodyError = true;
        else if(t->isNever()) bodyNever = true;
    }
    if(item->body->tail) {
        const Expr *tail = item->body->tail.get();
        SemTypePtr result = checkExpr(item->body->tail.get(), scope, sig ? sig->ret : nullptr);
        if(sig && sig->ret && !sig->ret->isError() && result && !result->isError() &&
           !result->isNever() && !canCoerce(result, sig->ret)) {
            error(tail->span, "function body type does not match return type");
        }
    }
    else if(!bodyError && !bodyNever && sig && sig->ret && !sig->ret->isError()) {
        //no tail, by return
        if(!canCoerce(SemType::makeUnit(), sig->ret)) {
            error(item->body->span, "function body type does not match return type");
        }
    }
    currentFuncRet = savedRet;
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

SemTypePtr SemanticAnalyzer::checkStmt(Stmt *stmt, Scope &scope) {
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
            return SemType::makeUnit();
        }
        case NodeType::ExprStmt: {
            auto *st = static_cast<ExprStmt*>(stmt);
            auto unit = SemType::makeUnit();
            auto tmp = \
                checkExpr(st->expr.get(), scope, st->hasSemi ? nullptr : unit);
            if(!st->hasSemi && tmp && !tmp->isError() && !tmp->isNever() && !canCoerce(tmp, unit)) {
                error(st->span, "block expr without semi must be unit type");
            }
            return tmp;
        }
        case NodeType::NullStmt:
        default:
            return SemType::makeUnit();
    }
}

SemTypePtr SemanticAnalyzer::checkExpr(Expr *expr, Scope &scope, const SemTypePtr &expected) {
    if(expr == nullptr) {
        return SemType::makeError();
    }
    SemTypePtr ret = checkExprNode(expr, scope, expected);
    result.exprTypes[expr] = ret;
    checkReborrow(expr, ret, expected, scope);
    return ret;
}

SemTypePtr SemanticAnalyzer::checkExprNode(Expr *expr, Scope &scope, const SemTypePtr &expected) {
    switch(expr->typ) {
        case NodeType::IntExpr:        return checkIntExpr(static_cast<IntExpr*>(expr), scope, expected);
        case NodeType::BoolExpr:       return checkBoolExpr(static_cast<BoolExpr*>(expr), scope, expected);
        case NodeType::UnitExpr:       return checkUnitExpr(static_cast<UnitExpr*>(expr), scope, expected);
        case NodeType::GroupExpr:      return checkGroupExpr(static_cast<GroupExpr*>(expr), scope, expected);
        case NodeType::UnaryExpr:      return checkUnaryExpr(static_cast<UnaryExpr*>(expr), scope, expected);
        case NodeType::BinaryExpr:     return checkBinaryExpr(static_cast<BinaryExpr*>(expr), scope, expected);
        case NodeType::AssignExpr:     return checkAssignExpr(static_cast<AssignExpr*>(expr), scope, expected);
        case NodeType::CastExpr:       return checkCastExpr(static_cast<CastExpr*>(expr), scope, expected);
        case NodeType::PathExpr:       return checkPathExpr(static_cast<PathExpr*>(expr), scope, expected);
        case NodeType::FieldExpr:      return checkFieldExpr(static_cast<FieldExpr*>(expr), scope, expected);
        case NodeType::IndexExpr:      return checkIndexExpr(static_cast<IndexExpr*>(expr), scope, expected);
        case NodeType::CallExpr:       return checkCallExpr(static_cast<CallExpr*>(expr), scope, expected);
        case NodeType::MethodCallExpr: return checkMethodCallExpr(static_cast<MethodCallExpr*>(expr), scope, expected);
        case NodeType::StructExpr:     return checkStructExpr(static_cast<StructExpr*>(expr), scope, expected);
        case NodeType::ArrayExpr:      return checkArrayExpr(static_cast<ArrayExpr*>(expr), scope, expected);
        case NodeType::IfExpr:         return checkIfExpr(static_cast<IfExpr*>(expr), scope, expected);
        case NodeType::WhileExpr:      return checkWhileExpr(static_cast<WhileExpr*>(expr), scope, expected);
        case NodeType::LoopExpr:       return checkLoopExpr(static_cast<LoopExpr*>(expr), scope, expected);
        case NodeType::BlockExpr:      return checkBlockExpr(static_cast<BlockExpr*>(expr), scope, expected);
        case NodeType::ReturnExpr:     return checkReturnExpr(static_cast<ReturnExpr*>(expr), scope, expected);
        case NodeType::BreakExpr:      return checkBreakExpr(static_cast<BreakExpr*>(expr), scope, expected);
        case NodeType::ContinueExpr:   return checkContinueExpr(static_cast<ContinueExpr*>(expr), scope, expected);
        default: break;
    }
    return SemType::makeError();
}

SemTypePtr SemanticAnalyzer::checkIntExpr(IntExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)scope;
    SemKind kind;
    switch(expr->val.suffix) {
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
        error(expr->span, "type mismatch");
    }
    return ret;
}

SemTypePtr SemanticAnalyzer::checkBoolExpr(BoolExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)scope;
    auto ret = SemType::make(SemKind::Bool);
    if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
        error(expr->span, "type mismatch");
    }
    return ret;
}

SemTypePtr SemanticAnalyzer::checkUnitExpr(UnitExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expr; (void)scope; (void)expected;
    return SemType::makeUnit();
}

SemTypePtr SemanticAnalyzer::checkGroupExpr(GroupExpr *expr, Scope &scope, const SemTypePtr &expected) {
    return checkExpr(expr->inner.get(), scope, expected);
}

SemTypePtr SemanticAnalyzer::checkUnaryExpr(UnaryExpr *expr, Scope &scope, const SemTypePtr &expected) {
    switch(expr->op) {
        case UnaryOp::Neg: {
            //expected does not propagate through unary operators
            auto op = checkExpr(expr->num.get(), scope, nullptr);
            if(!op || op->isError()) return SemType::makeError();
            auto operand = op;
            if(op->kind == SemKind::Ref) {
                if(op->isMut) {//-&mut T is not supported
                    error(expr->span, "unary minus does not support a mutable reference operand");
                    return SemType::makeError();
                }
                operand = op->inner;
            }
            if(!operand || operand->isError()) return SemType::makeError();
            if(!operand->isSignedInteger()) {
                error(expr->span, "unary minus is signed-only");
                return SemType::makeError();
            }
            auto ret = SemType::make(operand->kind);
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(expr->span, "type mismatch");
            }
            return ret;
        }
        case UnaryOp::Not: {
            auto op = checkExpr(expr->num.get(), scope, nullptr);
            if(!op || op->isError()) return SemType::makeError();
            auto operand = op;
            if(op->kind == SemKind::Ref) {
                if(op->isMut) {//-&mut T is not supported
                    error(expr->span, "unary neg does not support a mutable reference operand");
                    return SemType::makeError();
                }
                operand = op->inner;
            }
            if(!operand || operand->isError()) return SemType::makeError();
            if(!operand->isBool() && !operand->isInteger()) {
                error(expr->span, "unary neg is only supported by bool and int");
                return SemType::makeError();
            }
            auto ret = SemType::make(operand->kind);
            //!bool: logic not
            //!int: bitwise not (like ~ in C++)
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(expr->span, "type mismatch");
            }
            return ret;
        }
        case UnaryOp::Ref: {
            auto op = checkExpr(expr->num.get(), scope, nullptr);
            if(!op || op->isError()) return SemType::makeError();
            auto ret = SemType::makeRef(op, false);
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(expr->span, "type mismatch");
            }
            return ret;
        }
        case UnaryOp::Deref: {
            auto op = checkExpr(expr->num.get(), scope, nullptr);
            if(!op || op->isError()) return SemType::makeError();
            if(op->kind != SemKind::Ref && op->kind != SemKind::Box) {
                error(expr->span, "deref error: ref does not exist");
                return SemType::makeError();
            }
            auto ret = op->inner;
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(expr->span, "type mismatch");
            }
            return ret;
        }
        case UnaryOp::RefMut: {
            PlaceInfo place = placeInfo(expr->num.get(), scope);
            if(place.isPlace && !place.isMut) {//let x = 1; &mut x; should be rejected 
                error(expr->span, "cannot mutably borrow an immutable place");
                return SemType::makeError();
            }
            auto op = checkExpr(expr->num.get(), scope, nullptr);
            if(!op || op->isError()) return SemType::makeError();
            auto ret = SemType::makeRef(op, true);
            if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
                error(expr->span, "type mismatch");
            }
            return ret;
        }
    }
    return SemType::makeError();
}

static bool orderCompatible(const SemTypePtr &a, const SemTypePtr &b, bool outermost) {
    if(a->kind != SemKind::Ref && b->kind != SemKind::Ref) {
        return (a->isInteger() || a->isBool()) && sameType(a, b);
    }
    if(a->kind == SemKind::Ref && b->kind == SemKind::Ref) {
        bool mutOk = (a->isMut == b->isMut) || (outermost && !a->isMut && b->isMut);
        return mutOk && orderCompatible(a->inner, b->inner, false);
    }
    return false;
}


SemTypePtr SemanticAnalyzer::checkBinaryExpr(BinaryExpr *expr, Scope &scope, const SemTypePtr &expected) {
    auto lhs = checkExpr(expr->lhs.get(), scope, nullptr);
    auto rhs = checkExpr(expr->rhs.get(), scope, nullptr);
    if(!lhs || !rhs || lhs->isError() || rhs->isError()) {
        return SemType::makeError();
    }

    SemTypePtr ret, l = lhs, r = rhs;
    switch(expr->op) {
        case BinaryOp::Add:
        case BinaryOp::Sub:
        case BinaryOp::Mul:
        case BinaryOp::Div:
        case BinaryOp::Mod: {
            if(lhs->kind == SemKind::Ref) {
                if(lhs->isMut) {
                    error(expr->span, "binary op does not support a mutable reference operand");
                    return SemType::makeError();
                }
                l = lhs->inner;
            }
            if(rhs->kind == SemKind::Ref) {
                if(rhs->isMut) {
                    error(expr->span, "binary op does not support a mutable reference operand");
                    return SemType::makeError();
                }
                r = rhs->inner;
            }
            if(!l || !r) return SemType::makeError();
            if(!l->isInteger() || !sameType(l, r)) {
                error(expr->span, "arithmetic requires the same integer type on both sides");
                return SemType::makeError();
            }
            ret = SemType::make(l->kind);
            break;
        }
        case BinaryOp::BitAnd:
        case BinaryOp::BitOr:
        case BinaryOp::BitXor: {
            if(lhs->kind == SemKind::Ref) {
                if(lhs->isMut) {
                    error(expr->span, "binary op does not support a mutable reference operand");
                    return SemType::makeError();
                }
                l = lhs->inner;
            }
            if(rhs->kind == SemKind::Ref) {
                if(rhs->isMut) {
                    error(expr->span, "binary op does not support a mutable reference operand");
                    return SemType::makeError();
                }
                r = rhs->inner;
            }
            if(!l || !r) return SemType::makeError();
            if((!l->isInteger() && !l->isBool()) || !sameType(l, r)) {
                error(expr->span, "bitwise op requires the same integer or bool type on both sides");
                return SemType::makeError();
            }
            ret = SemType::make(l->kind);
            break;
        }
        case BinaryOp::Shl:
        case BinaryOp::Shr: {
            if(lhs->kind == SemKind::Ref) {
                if(lhs->isMut) {
                    error(expr->span, "binary op does not support a mutable reference operand");
                    return SemType::makeError();
                }
                l = lhs->inner;
            }
            if(rhs->kind == SemKind::Ref) {
                if(rhs->isMut) {
                    error(expr->span, "binary op does not support a mutable reference operand");
                    return SemType::makeError();
                }
                r = rhs->inner;
            }
            if(!l || !r) return SemType::makeError();
            if(!l->isInteger() || !r->isInteger()) {
                error(expr->span, "shift requires integer operands");
                return SemType::makeError();
            }
            ret = SemType::make(l->kind);//result has the left operand's type
            break;
        }
        case BinaryOp::Eq:
        case BinaryOp::Ne: {
            std::set<std::pair<const StructDefItem *, Derive>> vis;
            if(!sameType(lhs, rhs) || !typeSupportsDerive(lhs, Derive::PartialEq, vis)) {
                error(expr->span, "equality requires the same PartialEq type on both sides");
                return SemType::makeError();
            }
            ret = SemType::make(SemKind::Bool);
            break;
        }
        case BinaryOp::Lt:
        case BinaryOp::Le:
        case BinaryOp::Gt:
        case BinaryOp::Ge: {
            if(!orderCompatible(lhs, rhs, true)) {
                error(expr->span, "ordering requires matching scalar operands");
                return SemType::makeError();
            }
            ret = SemType::make(SemKind::Bool);
            break;
        }
        case BinaryOp::LogicalAnd:
        case BinaryOp::LogicalOr: {
            if(lhs->kind != SemKind::Bool || rhs->kind != SemKind::Bool) {
                error(expr->span, "lazy boolean operators require bool operands");
                return SemType::makeError();
            }
            ret = SemType::make(SemKind::Bool);
            break;
        }
    }
    if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
        error(expr->span, "type mismatch");
    }
    return ret;
}

SemTypePtr SemanticAnalyzer::checkAssignExpr(AssignExpr *expr, Scope &scope, const SemTypePtr &expected) {
    PlaceInfo info = placeInfo(expr->lhs.get(), scope);
    if(!info.isPlace) {
        error(expr->lhs->span, "assignment destination is not a place");
        return SemType::makeError();
    }
    if(!info.isMut) {
        error(expr->lhs->span, "cannot assign to an immutable place");
        return SemType::makeError();
    }
    auto lhs = checkExpr(expr->lhs.get(), scope, nullptr);
    SemTypePtr ret, l, r;
    switch(expr->op) {
        case AssignOp::Assign: {
            auto rhs = checkExpr(expr->rhs.get(), scope, lhs);//the destination type is the RHS expected type
            if(lhs && !lhs->isError() && rhs && !rhs->isError() &&
               !rhs->isNever() && !canCoerce(rhs, lhs)) {
                error(expr->rhs->span, "assigned value type does not match the destination type");
            }
            ret = SemType::makeUnit();
            break;
        }
        case AssignOp::Add:
        case AssignOp::Sub:
        case AssignOp::Mul: 
        case AssignOp::Div: 
        case AssignOp::Mod: {
            l = lhs;
            r = checkExpr(expr->rhs.get(), scope, nullptr);
            if(!l || l->isError() || !r || r->isError()) {
                ret = SemType::makeUnit();
                break;
            }
            if(r->kind == SemKind::Ref) {
                if(r->isMut) {
                    error(expr->span, "compound assignment does not support a mutable reference operand");
                    return SemType::makeError();
                }
                r = r->inner;
            }
            if(!l->isInteger() || !sameType(l, r)) {
                error(expr->span, "arithmetic compound assignment requires the same integer type on both sides");
                return SemType::makeError();
            }
            ret = SemType::makeUnit();
            break;
        }
        case AssignOp::BitAnd:
        case AssignOp::BitOr:
        case AssignOp::BitXor: {
            l = lhs;
            r = checkExpr(expr->rhs.get(), scope, nullptr);
            if(!l || l->isError() || !r || r->isError()) {
                ret = SemType::makeUnit();
                break;
            }
            if(r->kind == SemKind::Ref) {
                if(r->isMut) {
                    error(expr->span, "compound assignment does not support a mutable reference operand");
                    return SemType::makeError();
                }
                r = r->inner;
            }
            if((!l->isInteger() && !l->isBool()) || !sameType(l, r)) {
                error(expr->span, "bitwise compound assignment requires the same integer or bool type on both sides");
                return SemType::makeError();
            }
            ret = SemType::makeUnit();
            break;
        }
        case AssignOp::Shl:
        case AssignOp::Shr: {
            l = lhs;
            r = checkExpr(expr->rhs.get(), scope, nullptr);
            if(!l || l->isError() || !r || r->isError()) {
                ret = SemType::makeUnit();
                break;
            }
            if(r->kind == SemKind::Ref) {
                if(r->isMut) {
                    error(expr->span, "compound assignment does not support a mutable reference operand");
                    return SemType::makeError();
                }
                r = r->inner;
            }
            if(!l->isInteger() || !r->isInteger()) {
                error(expr->span, "shift compound assignment requires integer operands");
                return SemType::makeError();
            }
            ret = SemType::makeUnit();
            break;
        }
    }
    if(expected && !expected->isError() && !expected->isNever() && !canCoerce(ret, expected)) {
        error(expr->span, "type mismatch");
    }
    return ret;
}

SemTypePtr SemanticAnalyzer::checkCastExpr(CastExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    auto op = checkExpr(expr->expr.get(), scope, nullptr);
    if(!op || op->isError()) {
        return SemType::makeError();
    }
    if(!op->isInteger() && !op->isBool()) {
        error(expr->span, "cast op only supports bool and integer");
        return SemType::makeError();
    }
    auto typ = checkType(expr->type.get());
    if(!typ || typ->isError()) {
        return SemType::makeError();
    }
    if(!typ->isInteger()) {
        error(expr->span, "cast type only supports integer");
        return SemType::makeError();
    }
    return SemType::make(typ->kind);
}

SemTypePtr SemanticAnalyzer::checkPathExpr(PathExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    Symbol *sym = resolvePathSymbol(expr, scope);
    if(sym == nullptr) {
        return SemType::makeError();
    }
    if(sym->kind == SymbolKind::Fn || sym->kind == SymbolKind::BuiltinFn ||
       sym->kind == SymbolKind::AssocFn || sym->kind == SymbolKind::Method) {
        error(expr->span, "func is not a value");
        return SemType::makeError();
    }
    return sym->type;
}

SemTypePtr SemanticAnalyzer::checkFieldExpr(FieldExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    auto base = checkExpr(expr->base.get(), scope, nullptr);
    while(base && (base->kind == SemKind::Ref || base->kind == SemKind::Box)) {
        base = base->inner;
    }
    if(!base || base->isError()) {
        return SemType::makeError();
    }
    if(base->kind != SemKind::Struct) {
        error(expr->span, "invalid fieldExpr");
        return SemType::makeError();
    }
    auto def = base->structDef;
    auto it = result.structs.find(def);
    if(it == result.structs.end()) {
        error(expr->span, "struct in fieldExpr does not exist");
        return SemType::makeError();
    }
    const StructInfo &info = it->second;
    auto fit = info.fieldIndex.find(expr->field);
    if(fit == info.fieldIndex.end()) {
        error(expr->span, "unknown field: " + expr->field);
        return SemType::makeError();
    }
    return info.fields[fit->second].type;
}

SemTypePtr SemanticAnalyzer::checkIndexExpr(IndexExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    auto arr = checkExpr(expr->base.get(), scope, nullptr);
    while(arr && (arr->kind == SemKind::Ref || arr->kind == SemKind::Box)) {
        arr = arr->inner;
    }
    if(!arr || arr->isError()) {
        return SemType::makeError();
    }
    if(arr->kind != SemKind::Array && arr->kind != SemKind::Vec) {
        error(expr->span, "indexExpr: not an array or a vec");
        return SemType::makeError();
    }
    auto ind = checkExpr(expr->index.get(), scope, SemType::make(SemKind::Usize));
    if(!ind || ind->isError()) {
        return SemType::makeError();
    }
    if(ind->kind != SemKind::Usize) {
        error(expr->span, "index type should be Usize");
        return SemType::makeError();
    }
    if(arr->kind == SemKind::Array) {
        return arr->elem;
    }
    else {
        return arr->inner;
    }
}

Symbol* SemanticAnalyzer::resolvePathSymbol(Expr *expr, Scope &scope) {
    if(expr->typ != NodeType::PathExpr) {
        error(expr->span, "not a pathExpr");
        return nullptr;
    }
    auto *path = static_cast<PathExpr*>(expr);
    size_t segLen = path->segments.size();
    if(segLen > 2) {
        error(expr->span, "Rx does not have modules or associated types");
        return nullptr;
    }
    if(segLen == 0) {
        error(expr->span, "empty pathExpr");
        return nullptr;
    }

    if(segLen == 1) {
        const auto &seg = path->segments[0];
        if(seg.isSelf) {
            auto ret = scope.lookup("self");
            if(ret == nullptr) {
                error(expr->span, "self outside a method");
                return nullptr;
            }
            result.resolved[expr] = ret;
            return ret;
        }
        if(seg.isSelfType) {
            error(expr->span, "Self is not a value");
            return nullptr;
        }
        if(!seg.typeArgs.empty()) {
            error(expr->span, "functions have no type parameters");
            return nullptr;
        }
        auto t1 = scope.lookup(seg.ident);
        if(t1 != nullptr) {
            result.resolved[expr] = t1;
            return t1;
        }
        auto t2 = lookupValue(seg.ident);
        if(t2 == nullptr) {
            error(expr->span, "unresolved name");
            return nullptr;
        }
        result.resolved[expr] = t2;
        return t2;
    }

    const auto &s0 = path->segments[0], &s1 = path->segments[1];
    if(s1.isSelf || s1.isSelfType || !s1.typeArgs.empty()) {
        error(expr->span, "invalid pathExpr");
        return nullptr;
    }
    const StructDefItem* def = nullptr;
    if(s0.isSelf) {
        error(expr->span, "Rx does not have modules(self::)");
        return nullptr;
    }
    else if(s0.isSelfType) {
        def = currentImplSelf;
        if(def == nullptr) {
            error(expr->span, "Self outside a struct or impl");
            return nullptr;
        }
    }
    else {
        auto cit = containerMembers.find(s0.ident);
        if(cit != containerMembers.end()) {
            if(s0.typeArgs.empty()) {
                return nullptr;
            }
            if(s0.typeArgs.size() != 1) {
                error(expr->span, "container type expects one type argument");
                return nullptr;
            }
            auto mit = cit->second.find(s1.ident);
            if(mit == cit->second.end()) {
                error(expr->span, "no associated item: " + s1.ident);
                return nullptr;
            }
            SemTypePtr T = checkType(s0.typeArgs[0].get());
            if(!T || T->isError()) {
                return nullptr;
            }
            if(s1.ident == "clone") {
                std::set<std::pair<const StructDefItem *, Derive>> vis;
                if(!typeSupportsDerive(T, Derive::Clone, vis)) {
                    error(expr->span, "clone requires a Clone element type");
                    return nullptr;
                }
            }
            Symbol *mem = getContainerMember(s0.ident, s1.ident, mit->second, T, expr->span);
            result.resolved[expr] = mem;
            return mem;
        }
        auto structTyp = lookupType(s0.ident);
        if(structTyp == nullptr || structTyp->kind != SymbolKind::Struct) {
            error(expr->span, "struct in pathExpr does not exist");
            return nullptr;
        }
        def = static_cast<const StructDefItem*>(structTyp->decl);
    }

    auto it = result.structs.find(def);
    if(it == result.structs.end()) {
        error(expr->span, "struct in pathExpr does not exist");
        return nullptr;
    }
    auto tmp = it->second.members.find(s1.ident);
    if(tmp == it->second.members.end()) {
        error(expr->span, "associated value in pathExpr does not exist");
        return nullptr;
    }

    result.resolved[expr] = tmp->second;
    return tmp->second;
}

SemTypePtr SemanticAnalyzer::checkCallExpr(CallExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    Symbol *sym = resolvePathSymbol(expr->callee.get(), scope);
    if(sym == nullptr) {
        return SemType::makeError();
    }
    const FuncSig *sig = funcSig(sym);
    if(sig == nullptr) {
        error(expr->span, "not callable");
        return SemType::makeError();
    }
    result.resolved[expr] = sym;
    size_t base = sig->hasSelf ? 1 : 0;
    if(sig->hasSelf && !expr->args.empty()) {
        SemTypePtr recv = sig->selfByRef ? SemType::makeRef(sym->type, sig->selfMut) : sym->type;
        auto arg = checkExpr(expr->args[0].get(), scope, recv);
        if(recv && !recv->isError() && arg && !arg->isError() &&
           !arg->isNever() && !canCoerce(arg, recv)) {
            error(expr->args[0]->span, "receiver type does not match");
        }
    }
    return checkCallArgs(sig, base, expr->args, scope, expr->span);
}

SemTypePtr SemanticAnalyzer::checkCallArgs(const FuncSig *sig, std::size_t base,
                                           const std::vector<NodePtr<Expr>> &args,
                                           Scope &scope, SourceSpan span) {
    std::size_t len = sig->params.size() + base;
    if(args.size() < len) {
        error(span, "too few arguments");
    }
    else if(args.size() > len) {
        error(span, "too many arguments");
    }
    for(size_t i = 0; i < sig->params.size() && i + base < args.size(); i++) {
        const SemTypePtr &paramType = sig->params[i];
        auto arg = checkExpr(args[i + base].get(), scope, paramType);
        if(paramType && !paramType->isError() && arg && !arg->isError() &&
           !arg->isNever() && !canCoerce(arg, paramType)) {
            error(args[i + base]->span, "argument type does not match");
        }
    }
    return sig->ret;
}

void SemanticAnalyzer::checkReborrow(Expr *expr, const SemTypePtr &type,
                                     const SemTypePtr &expected, Scope &scope) {
    if(!expected || expected->kind != SemKind::Ref || !expected->isMut) return;
    if(!type || type->kind != SemKind::Ref || !type->isMut) return;
    PlaceInfo place = placeInfo(expr, scope);
    if(place.isPlace && place.gated) {
        error(expr->span, "cannot reborrow a mutable reference through an immutable access");
    }
}

SemTypePtr SemanticAnalyzer::checkMethodCallExpr(MethodCallExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    if(!expr->methodTypeArgs.empty()) {
        error(expr->span, "method segments have no type parameters");
        return SemType::makeError();
    }
    SemTypePtr recvType = checkExpr(expr->receiver.get(), scope, nullptr);
    if(!recvType || recvType->isError()) {
        return SemType::makeError();
    }
    Symbol *sym = nullptr;
    SemTypePtr cur = recvType;
    MethodAdjust adjust;
    while(true) {//candidate chain(because receiver can have several forms: &mut, &, ...)
        sym = findMethod(cur, expr->method, expr->span);
        if(sym != nullptr) break;
        sym = findMethod(SemType::makeRef(cur, false), expr->method, expr->span);
        if(sym != nullptr) {
            adjust.borrow = MethodAdjust::Borrow::Shared;
            break;
        }
        sym = findMethod(SemType::makeRef(cur, true), expr->method, expr->span);
        if(sym != nullptr) {
            adjust.borrow = MethodAdjust::Borrow::Mut;
            break;
        }
        if(cur->kind == SemKind::Ref || cur->kind == SemKind::Box) {
            cur = cur->inner;
            adjust.derefs++;
        }
        else break;
    }
    if(sym == nullptr) {
        error(expr->span, "no matching method: " + expr->method);
        return SemType::makeError();
    }
    result.resolved[expr] = sym;
    result.adjustments[expr] = adjust;
    const FuncSig *sig = funcSig(sym);
    if(sig == nullptr) {
        return SemType::makeError();
    }
    if(sig->hasSelf && sig->selfMut) {
        PlaceInfo place = placeInfo(expr->receiver.get(), scope);
        if(place.isPlace && (place.gated || !placeDerefMut(recvType, place.isMut))) {
            error(expr->span, "mutable receiver needs a mutable place");
        }
    }
    return checkCallArgs(sig, 0, expr->args, scope, expr->span);
}

SemTypePtr SemanticAnalyzer::checkStructExpr(StructExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    const auto &segs = expr->path->segments;
    if(segs.size() != 1) {
        error(expr->span, "invalid struct construction path");
        return SemType::makeError();
    }
    const StructDefItem *def = nullptr;
    const PathSegment &seg = segs[0];
    if(seg.isSelfType) {
        def = currentImplSelf;
        if(def == nullptr) {
            error(expr->span, "Self outside a struct or impl");
            return SemType::makeError();
        }
    }
    else if(seg.isSelf) {
        error(expr->span, "self is not a type");
        return SemType::makeError();
    }
    else {
        Symbol *ts = lookupType(seg.ident);
        if(ts == nullptr || ts->kind != SymbolKind::Struct) {
            error(expr->span, "not a struct: " + seg.ident);
            return SemType::makeError();
        }
        def = static_cast<const StructDefItem *>(ts->decl);
    }
    auto it = result.structs.find(def);
    if(it == result.structs.end()) {
        error(expr->span, "unknown struct");
        return SemType::makeError();
    }
    Symbol *structSym = lookupType(def->name);
    if(structSym != nullptr) {
        result.resolved[expr] = structSym;
    }
    const StructInfo &info = it->second;

    std::vector<bool> vis(info.fields.size(), false);
    for(auto &tmp: expr->fields) {
        auto *field = tmp.get();
        auto fit = info.fieldIndex.find(field->name);
        if(fit == info.fieldIndex.end()) {
            error(field->span, "unknown field: " + field->name);
            continue;
        }
        std::size_t idx = fit->second;
        if(vis[idx]) {
            error(field->span, "duplicate field: " + field->name);
            continue;
        }
        vis[idx] = true;
        const SemTypePtr &fieldType = info.fields[idx].type;
        auto val = checkExpr(field->value.get(), scope, fieldType);
        if(fieldType && !fieldType->isError() && val && !val->isError() &&
           !val->isNever() && !canCoerce(val, fieldType)) {
            error(field->value->span, "field initializer type does not match the field type");
        }
    }
    for(std::size_t i = 0; i < info.fields.size(); i++) {
        if(!vis[i]) {
            error(expr->span, "missing field: " + info.fields[i].name);
        }
    }
    return SemType::makeStruct(def);
}

SemTypePtr SemanticAnalyzer::checkArrayExpr(ArrayExpr *expr, Scope &scope, const SemTypePtr &expected) {
    SemTypePtr elemExpected = (expected && expected->kind == SemKind::Array) ? expected->elem : nullptr;
    if(expr->isRepeat) {
        auto len = constEval.eval(expr->repeatLen.get());
        if(!len || len->kind != ConstKind::Int) {
            error(expr->repeatLen->span, "repeat length is not an integer const");
            return SemType::makeError();
        }
        if(!constExprIsUsize(expr->repeatLen.get())) {
            error(expr->repeatLen->span, "repeat length must have usize type");
            return SemType::makeError();
        }
        auto elem = checkExpr(expr->repeatValue.get(), scope, elemExpected);
        if(!elem || elem->isError()) {
            return SemType::makeError();
        }
        if(len->val.val > 1) {//len>1: Copy is required
            std::set<std::pair<const StructDefItem *, Derive>> vis;
            if(!typeSupportsDerive(elem, Derive::Copy, vis)) {
                error(expr->span, "repeating more than once requires a Copy element type");
            }
        }
        return SemType::makeArray(elem, static_cast<std::size_t>(len->val.val));
    }

    std::vector<SemTypePtr> elemTypes;
    for(auto &e : expr->elems) {
        auto t = checkExpr(e.get(), scope, elemExpected);
        if(!t || t->isError()) {
            return SemType::makeError();
        }
        if(elemExpected && !elemExpected->isError() && !t->isNever() &&
           !canCoerce(t, elemExpected)) {
            error(e->span, "array element type does not match the expected element type");
        }
        elemTypes.push_back(t);
    }
    SemTypePtr elem = elemExpected ? elemExpected : commonType(elemTypes);
    if(!elem || elem->isError()) {
        return SemType::makeError();
    }
    return SemType::makeArray(elem, expr->elems.size());
}

SemTypePtr SemanticAnalyzer::checkIfExpr(IfExpr *expr, Scope &scope, const SemTypePtr &expected) {
    SemTypePtr cond = checkExpr(expr->cond.get(), scope, SemType::make(SemKind::Bool));
    if(cond && !cond->isError() && !cond->isNever() && cond->kind != SemKind::Bool) {
        error(expr->cond->span, "if condition must be bool");
    }
    if(!expr->elseBranch) {
        auto bodyType = checkBlockExpr(expr->thenBlock.get(), scope, SemType::makeUnit());
        if(!bodyType || bodyType->isError()) {
            return SemType::makeError();
        }
        return SemType::makeUnit();
    }
    auto bodyType = checkBlockExpr(expr->thenBlock.get(), scope, expected);
    if(!bodyType || bodyType->isError()) {
        return SemType::makeError();
    }
    auto elseType = checkExpr(expr->elseBranch.get(), scope, expected);
    if(!elseType || elseType->isError()) {
        return SemType::makeError();
    }
    if(expected) {
        if(!expected->isError()) {
            if(!bodyType->isNever() && !canCoerce(bodyType, expected)) {
                error(expr->thenBlock->span, "if branch type does not match the expected type");
            }
            if(!elseType->isNever() && !canCoerce(elseType, expected)) {
                error(expr->elseBranch->span, "else branch type does not match the expected type");
            }
        }
        return expected;
    }
    return commonType({bodyType, elseType});
}

SemTypePtr SemanticAnalyzer::checkWhileExpr(WhileExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    loopStack.push_back(LoopCtx{nullptr, {}, false, true});
    SemTypePtr cond = checkExpr(expr->cond.get(), scope, SemType::make(SemKind::Bool));
    if(cond && !cond->isError() && !cond->isNever() && cond->kind != SemKind::Bool) {
        error(expr->cond->span, "while condition must be bool");
    }
    loopStack.pop_back();
    loopStack.push_back(LoopCtx{nullptr, {}, true});
    auto bodyType = checkBlockExpr(expr->body.get(), scope, SemType::makeUnit());
    if(!bodyType || bodyType->isError()) {
        loopStack.pop_back();
        return SemType::makeError();
    }
    loopStack.pop_back();
    return SemType::makeUnit();
}

SemTypePtr SemanticAnalyzer::checkLoopExpr(LoopExpr *expr, Scope &scope, const SemTypePtr &expected) {
    loopStack.push_back(LoopCtx{expected, {}, false});
    auto bodyType = checkBlockExpr(expr->body.get(), scope, SemType::makeUnit());
    LoopCtx ctx = std::move(loopStack.back());
    loopStack.pop_back();
    if(!bodyType || bodyType->isError()) {
        return SemType::makeError();
    }
    if(ctx.breaks.empty()) {
        return SemType::makeNever();
    }
    if(expected) {
        return expected;
    }
    return commonType(ctx.breaks);
}

SemTypePtr SemanticAnalyzer::checkBlockExpr(BlockExpr *expr, Scope &scope, const SemTypePtr &expected) {
    Scope child(&scope);
    bool bodyError = false, bodyNever = false;
    for(auto &stmt: expr->statements) {
        SemTypePtr t = checkStmt(stmt.get(), child);
        if(!t || t->isError()) bodyError = true;
        else if(t->isNever()) bodyNever = true;
    }
    if(bodyError) {
        return SemType::makeError();
    }
    if(expr->tail) {
        SemTypePtr ret = checkExpr(expr->tail.get(), child, expected);
        if(!ret || ret->isError()) {
            return SemType::makeError();
        }
        if(bodyNever) {
            return SemType::makeNever();
        }
        return ret;
    }
    return bodyNever ? SemType::makeNever() : SemType::makeUnit();
}

SemTypePtr SemanticAnalyzer::checkReturnExpr(ReturnExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    const SemTypePtr &ret = currentFuncRet;
    if(expr->val == nullptr) {
        SemTypePtr unit = SemType::makeUnit();
        if(ret && !ret->isError() && !ret->isNever() && !canCoerce(unit, ret)) {
            error(expr->span, "return without a value in a non-unit function");
        }
    }
    else {
        SemTypePtr val = checkExpr(expr->val.get(), scope, ret);
        if(ret && !ret->isError() && val && !val->isError() &&
           !val->isNever() && !canCoerce(val, ret)) {
            error(expr->val->span, "return value type does not match the function return type");
        }
    }
    return SemType::makeNever();
}

SemTypePtr SemanticAnalyzer::checkBreakExpr(BreakExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)expected;
    if(loopStack.empty() || loopStack.back().whileBarrier) {
        error(expr->span, "break outside a loop or in while cond");
        return SemType::makeError();
    }
    bool isWhile = loopStack.back().isWhile;
    SemTypePtr loopExpected = loopStack.back().expected;
    if(expr->val == nullptr) {
        loopStack.back().breaks.push_back(SemType::makeUnit());
        return SemType::makeNever();
    }
    if(isWhile) {
        error(expr->span, "a while cannot accept a break value");
        return SemType::makeError();
    }
    auto val = checkExpr(expr->val.get(), scope, loopExpected);
    if(!val || val->isError()) {
        return SemType::makeError();
    }
    if(loopExpected && !loopExpected->isError() && !val->isNever() &&
       !canCoerce(val, loopExpected)) {
        error(expr->val->span, "break value type does not match the loop expected type");
    }
    loopStack.back().breaks.push_back(val);
    return SemType::makeNever();
}

SemTypePtr SemanticAnalyzer::checkContinueExpr(ContinueExpr *expr, Scope &scope, const SemTypePtr &expected) {
    (void)scope;
    (void)expected;
    if(loopStack.empty() || loopStack.back().whileBarrier) {
        error(expr->span, "continue outside a loop or in while cond");
        return SemType::makeError();
    }
    return SemType::makeNever();
}

static bool placeDerefMut(const SemTypePtr &type, bool baseMut) {
    if(!type || type->isError()) return true;//unknown: assume mutable (avoid false reject)
    bool hasRef = false, allMut = true;
    SemTypePtr t = type;
    while(t && (t->kind == SemKind::Ref || t->kind == SemKind::Box)) {
        if(t->kind == SemKind::Ref) {
            hasRef = true;
            if(!t->isMut) allMut = false;
        }
        t = t->inner;
    }
    return hasRef ? allMut : baseMut;
}

static bool crossesShared(const SemTypePtr &type) {
    SemTypePtr t = type;
    while(t && (t->kind == SemKind::Ref || t->kind == SemKind::Box)) {
        if(t->kind == SemKind::Ref && !t->isMut) return true;
        t = t->inner;
    }
    return false;
}

static bool underlyingVec(const SemTypePtr &type) {
    SemTypePtr t = type;
    while(t && (t->kind == SemKind::Ref || t->kind == SemKind::Box)) t = t->inner;
    return t && t->kind == SemKind::Vec;
}

PlaceInfo SemanticAnalyzer::placeInfo(Expr *expr, Scope &scope) {
    if(expr == nullptr) return {};
    switch(expr->typ) {
        case NodeType::GroupExpr: {
            return placeInfo(static_cast<GroupExpr*>(expr)->inner.get(), scope);
        }
        case NodeType::PathExpr: {
            auto *path = static_cast<PathExpr*>(expr);
            if(path->segments.size() != 1) return {};
            const PathSegment &seg = path->segments[0];
            Symbol *sym = nullptr;
            if(seg.isSelf) {
                sym = scope.lookup("self");
            }
            else if(!seg.isSelfType) {
                sym = scope.lookup(seg.ident);
                if(sym == nullptr) sym = lookupValue(seg.ident);
            }
            if(sym == nullptr || !sym->isPlace) return {};
            return {true, sym->isMut, false};
        }
        case NodeType::FieldExpr: {
            auto *field = static_cast<FieldExpr*>(expr);
            PlaceInfo base = placeInfo(field->base.get(), scope);
            if(!base.isPlace) {
                return {true, true, false};//a field of an rvalue temporary is a mutable place
            }
            if(base.gated) {
                return {true, false, true};
            }
            SemTypePtr bt = placeTypeOf(field->base.get(), scope);
            bool mut = placeDerefMut(bt, base.isMut);
            return {true, mut, crossesShared(bt) || (underlyingVec(bt) && !mut)};
        }
        case NodeType::IndexExpr: {
            auto *index = static_cast<IndexExpr*>(expr);
            PlaceInfo base = placeInfo(index->base.get(), scope);
            if(!base.isPlace) {
                return {true, true, false};
            }
            if(base.gated) {
                return {true, false, true};
            }
            SemTypePtr bt = placeTypeOf(index->base.get(), scope);
            bool mut = placeDerefMut(bt, base.isMut);
            return {true, mut, crossesShared(bt) || (underlyingVec(bt) && !mut)};
        }
        case NodeType::UnaryExpr: {
            auto *unary = static_cast<UnaryExpr*>(expr);
            if(unary->op != UnaryOp::Deref) return {};
            PlaceInfo base = placeInfo(unary->num.get(), scope);
            if(base.gated) {
                return {true, false, true};
            }
            SemTypePtr t = placeTypeOf(unary->num.get(), scope);
            if(!t || t->isError()) {//unknown operand type: assume mutable
                return {true, true, false};
            }
            if(t->kind == SemKind::Ref) {//&mut x/&x deref to a place; a shared deref gates below
                return {true, t->isMut, !t->isMut};
            }
            if(t->kind == SemKind::Box) {
                return {true, base.isMut, base.gated};//a Box inherits its owner place's mutability
            }
            return {};//scalars/Vec/ ... are not dereferenceable
        }
        default:
            return {};
    }
}

SemTypePtr SemanticAnalyzer::placeTypeOf(Expr *expr, Scope &scope) {
    if(expr == nullptr) return nullptr;
    switch(expr->typ) {
        case NodeType::GroupExpr:
            return placeTypeOf(static_cast<GroupExpr*>(expr)->inner.get(), scope);
        case NodeType::PathExpr: {
            auto *path = static_cast<PathExpr*>(expr);
            if(path->segments.size() != 1) return nullptr;
            const PathSegment &seg = path->segments[0];
            Symbol *sym = nullptr;
            if(seg.isSelf) {
                sym = scope.lookup("self");
            }
            else if(!seg.isSelfType) {
                sym = scope.lookup(seg.ident);
                if(sym == nullptr) sym = lookupValue(seg.ident);
            }
            return sym ? sym->type : nullptr;
        }
        case NodeType::FieldExpr: {
            auto *field = static_cast<FieldExpr*>(expr);
            SemTypePtr base = placeTypeOf(field->base.get(), scope);
            while(base && (base->kind == SemKind::Ref || base->kind == SemKind::Box)) base = base->inner;
            if(!base || base->kind != SemKind::Struct) return nullptr;
            auto it = result.structs.find(base->structDef);
            if(it == result.structs.end()) return nullptr;
            auto fit = it->second.fieldIndex.find(field->field);
            if(fit == it->second.fieldIndex.end()) return nullptr;
            return it->second.fields[fit->second].type;
        }
        case NodeType::IndexExpr: {
            auto *index = static_cast<IndexExpr*>(expr);
            SemTypePtr base = placeTypeOf(index->base.get(), scope);
            while(base && (base->kind == SemKind::Ref || base->kind == SemKind::Box)) base = base->inner;
            if(!base) return nullptr;
            if(base->kind == SemKind::Array) return base->elem;
            if(base->kind == SemKind::Vec) return base->inner;
            return nullptr;
        }
        case NodeType::UnaryExpr: {
            auto *unary = static_cast<UnaryExpr*>(expr);
            if(unary->op != UnaryOp::Deref) return nullptr;
            SemTypePtr t = placeTypeOf(unary->num.get(), scope);
            if(t && (t->kind == SemKind::Ref || t->kind == SemKind::Box)) return t->inner;
            return nullptr;
        }
        default:
            return nullptr;
    }
}

TypeLayout SemanticAnalyzer::layoutOf(const SemTypePtr &type) const {
    if(type == nullptr) {
        return {0, 1};
    }
    switch(type->kind) {
        case SemKind::I32:
        case SemKind::U32:
        case SemKind::Isize:
        case SemKind::Usize: {
            return {4, 4};
        }
        case SemKind::Bool: {
            return {1, 1};
        }
        case SemKind::Unit:
        case SemKind::Never: {
            return {0, 1};
        }
        case SemKind::Box://32 bit sys, 4 byte
        case SemKind::Ref: {
            return {4, 4};
        }
        case SemKind::Vec: {//{data_ptr, len, cap}
            return {12, 4};
        }
        case SemKind::Array: {
            TypeLayout elem = layoutOf(type->elem);
            return {elem.size * type->arrayLen, elem.align};
        }
        case SemKind::Struct: {
            auto it = result.structs.find(type->structDef);
            if(it == result.structs.end()) {
                return {0, 1};
            }
            else {
                return it->second.layout;
            }
        }
        default: {
            return {0, 1};
        }
    }
}
