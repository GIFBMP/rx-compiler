#pragma once
#include "../AST/AstNode.h"
#include "semtype.h"
#include <string>

enum class SymbolKind {
    Local,       //let
    Param,       //selfparam/funcparam
    Fn,          //global function
    Const,       //global const
    Struct,      //global struct
    AssocFn,     //func in struct(not impl)
    AssocConst,  //const in struct
    Method,
    BuiltinType, //builtin type name (i32/u32/isize/usize/bool/Box/Vec)
    BuiltinFn,   //builtin function (get_i32/print_i32/println_i32)
};

struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Local;
    SourceSpan span{};
    const AstNode *decl = nullptr;
    SemTypePtr type;
    bool isMut = false;
    bool isPlace = false;  //is assignable(eg. local/param)
    bool isSelf = false;
};
