#pragma once
#include "symbol.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

//only local/params
//global func/const/struct will be processed in SemanticAnalyzer

class Scope {
public:
    explicit Scope(Scope *parent = nullptr)
        : parent(parent), arena(parent ? parent->arena : nullptr) {}

    Scope(Scope *parent, std::vector<std::unique_ptr<Symbol>> &arena)
        : parent(parent), arena(&arena) {}

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
        if(arena != nullptr) {
            arena->push_back(std::move(sym));
        }
        else {
            owned.push_back(std::move(sym));
        }
        return raw;
    }

    Scope *parentScope() const {
        return parent;
    }

private:
    Scope *parent;
    std::vector<std::unique_ptr<Symbol>> *arena = nullptr;
    std::unordered_map<std::string, Symbol *> names;
    std::vector<std::unique_ptr<Symbol>> owned;
};
