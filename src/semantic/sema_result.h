#pragma once
#include "../AST/AstNode.h"
#include "../AST/item.h"
#include "constEval.h"
#include "semtype.h"
#include "symbol.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace rx::semantic {
using namespace rx::ast;

struct FuncSig {
    std::vector<SemTypePtr> params;
    SemTypePtr ret;
    bool hasSelf = false;
    bool selfByRef = false;
    bool selfMut = false;
};

struct TypeLayout {
    size_t size = 0;
    size_t align = 1;
};

struct StructInfo {
    const StructDefItem *def = nullptr;

    struct Field {
        std::string name;
        SemTypePtr type;
        SourceSpan span{};
    };

    std::vector<Field> fields;
    std::unordered_map<std::string, std::size_t> fieldIndex;
    std::unordered_map<std::string, Symbol *> members;
    std::vector<Derive> derives;

    std::vector<size_t> fieldOffsets;
    TypeLayout layout;
};

inline size_t alignTo(size_t val, size_t align) {// val -> real address(aligned)
    return (val + align - 1) / align * align;
}

struct MethodAdjust {
    int derefs = 0;
    enum class Borrow { None, Shared, Mut } borrow = Borrow::None;
};

enum class BuiltinOp {
    None,
    BoxNew, BoxClone, VecNew, VecClone, VecLen, VecIsEmpty,
    VecPush, VecRemove, Clone, ArrayLen, PrintI32, PrintlnI32, GetI32
};

struct SemaResult {
    std::unordered_map<const AstNode *, SemTypePtr> exprTypes;
    std::unordered_map<const AstNode *, Symbol *> resolved;
    std::unordered_map<const AstNode *, MethodAdjust> adjustments;
    std::unordered_map<const Symbol *, BuiltinOp> builtinOps;
    std::unordered_map<const StructDefItem *, StructInfo> structs;
    std::unordered_map<const Symbol *, FuncSig> sigs;
    std::unordered_map<const Symbol *, ConstValue> constValues;
    std::vector<std::unique_ptr<Symbol>> ownedSymbols;

    SemTypePtr typeOf(const AstNode *node) const {
        auto it = exprTypes.find(node);
        return it == exprTypes.end() ? nullptr : it->second;
    }

    Symbol *symbolOf(const AstNode *node) const {
        auto it = resolved.find(node);
        return it == resolved.end() ? nullptr : it->second;
    }

    MethodAdjust adjustOf(const AstNode *node) const {
        auto it = adjustments.find(node);
        return it == adjustments.end() ? MethodAdjust{} : it->second;
    }

    BuiltinOp builtinOpOf(const Symbol *sym) const {
        auto it = builtinOps.find(sym);
        return it == builtinOps.end() ? BuiltinOp::None : it->second;
    }
};
} // namespace rx::semantic
