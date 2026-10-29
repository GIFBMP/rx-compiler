#pragma once
#include "symbol.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace rx::semantic {
using namespace rx::ast;

//only local/params
//global func/const/struct will be processed in SemanticAnalyzer

class Scope {
public:
    explicit Scope(std::vector<std::unique_ptr<Symbol>> &arena) : arena(&arena) {}

    explicit Scope(Scope *parent) : parent(parent), arena(parent->arena) {}

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

    Symbol *lookupLocal(const std::string &name) const {
        auto it = names.find(name);
        return it == names.end() ? nullptr : it->second;
    }

    Symbol *lookup(const std::string &name) const {
        for(const Scope *s = this; s != nullptr; s = s->parent) {
            if(Symbol *sym = s->lookupLocal(name)) {
                return sym;
            }
        }
        return nullptr;
    }

    Symbol *define(std::unique_ptr<Symbol> sym) {
        Symbol *raw = sym.get();
        names[raw->name] = raw;
        arena->push_back(std::move(sym));
        return raw;
    }

    Scope *parentScope() const {
        return parent;
    }

private:
    Scope *parent = nullptr;
    std::vector<std::unique_ptr<Symbol>> *arena = nullptr;
    //arena: keep the ptr of Symbols after the Scope is deconstructed
    std::unordered_map<std::string, Symbol *> names;
};
} // namespace rx::semantic
