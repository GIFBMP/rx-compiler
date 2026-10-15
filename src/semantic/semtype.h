#pragma once
#include <cstddef>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct StructDefItem;

enum class SemKind {
    I32, U32, Isize, Usize, Bool,
    Unit,
    Never,
    Ref,
    Array,
    Struct,
    Box, Vec,
    Error,
};

struct SemType;
using SemTypePtr = std::shared_ptr<SemType>;

struct SemType {
    SemKind kind = SemKind::Error;
    bool isMut = false;
    SemTypePtr inner;
    SemTypePtr elem;
    std::size_t arrayLen = 0;
    const StructDefItem *structDef = nullptr;

    static SemTypePtr make(SemKind k) {
        auto t = std::make_shared<SemType>();
        t->kind = k;
        return t;
    }
    static SemTypePtr makeRef(const SemTypePtr &inner, bool isMut) {
        auto t = make(SemKind::Ref);
        t->inner = inner;
        t->isMut = isMut;
        return t;
    }
    static SemTypePtr makeArray(const SemTypePtr &elem, std::size_t len) {
        auto t = make(SemKind::Array);
        t->elem = elem;
        t->arrayLen = len;
        return t;
    }
    static SemTypePtr makeContainer(SemKind k, const SemTypePtr &inner) {
        auto t = make(k);
        t->inner = inner;
        return t;
    }
    static SemTypePtr makeStruct(const StructDefItem *def) {
        auto t = make(SemKind::Struct);
        t->structDef = def;
        return t;
    }
    static SemTypePtr makeError() {
        return make(SemKind::Error);
    }
    static SemTypePtr makeUnit() {
        return make(SemKind::Unit);
    }
    static SemTypePtr makeNever() {
        return make(SemKind::Never);
    }

    bool isInteger() const {
        return kind == SemKind::I32 || kind == SemKind::U32 ||
               kind == SemKind::Isize || kind == SemKind::Usize;
    }
    bool isSignedInteger() const {
        return kind == SemKind::I32 || kind == SemKind::Isize;
    }
    bool isBool() const {
        return kind == SemKind::Bool;
    }
    bool isError() const {
        return kind == SemKind::Error;
    }
    bool isNever() const {
        return kind == SemKind::Never;
    }
};

inline bool sameType(const SemTypePtr &a, const SemTypePtr &b) {
    if(a == b) return true;
    if(!a || !b) return false;
    if(a->kind != b->kind) return false;
    switch(a->kind) {
        case SemKind::Ref:
            return a->isMut == b->isMut && sameType(a->inner, b->inner);
        case SemKind::Array:
            return a->arrayLen == b->arrayLen && sameType(a->elem, b->elem);
        case SemKind::Box:
        case SemKind::Vec:
            return sameType(a->inner, b->inner);
        case SemKind::Struct:
            return a->structDef == b->structDef;
        default:
            return true;
    }
}

inline bool canCoerce(const SemTypePtr &src, const SemTypePtr &dst) {
    if(!src || !dst) return true;
    if(src->isError() || dst->isError()) return true;
    if(src->kind == SemKind::Never) return true;
    if(sameType(src, dst)) return true;

    if(src->kind != SemKind::Ref || dst->kind != SemKind::Ref) return false;
    if(dst->isMut && !src->isMut) return false;

    SemTypePtr cur = src->inner;
    while(cur) {
        if(sameType(cur, dst->inner)) return true;
        if(cur->kind == SemKind::Ref) {
            if(dst->isMut && !cur->isMut) return false;
            cur = cur->inner;
        }
        else if(cur->kind == SemKind::Box) {
            cur = cur->inner;
        }
        else {
            return false;
        }
    }
    return false;
}

inline SemTypePtr commonType(const std::vector<SemTypePtr> &results) {
    std::vector<SemTypePtr> rs;
    for(const SemTypePtr &t : results) {
        if(!t || t->isError()) return SemType::makeError();
        if(t->isNever()) continue;
        rs.push_back(t);
    }
    if(rs.empty()) return SemType::makeNever();

    SemTypePtr target = rs[0];
    for(size_t i = 1; i < rs.size(); i++) {
        const SemTypePtr &tmp = rs[i];
        if(canCoerce(tmp, target)) continue;
        bool flag = canCoerce(target, tmp);
        for(size_t j = 0; j < i; j++) {
            if(!canCoerce(rs[j], tmp)) {
                flag = false;
                break;
            }
        }
        if(flag) {
            target = tmp;
        }
        else {
            return SemType::makeError();
        }
    }
    return target;
}

inline std::string typeToString(const SemTypePtr &t) {
    if(!t) return "<null>";
    switch(t->kind) {
        case SemKind::I32:   return "i32";
        case SemKind::U32:   return "u32";
        case SemKind::Isize: return "isize";
        case SemKind::Usize: return "usize";
        case SemKind::Bool:  return "bool";
        case SemKind::Unit:  return "()";
        case SemKind::Never: return "!";
        case SemKind::Ref:
            return std::string(t->isMut ? "&mut " : "&") + typeToString(t->inner);
        case SemKind::Array: {
            std::ostringstream os;
            os << "[" << typeToString(t->elem) << "; " << t->arrayLen << "]";
            return os.str();
        }
        case SemKind::Box:    return "Box<" + typeToString(t->inner) + ">";
        case SemKind::Vec:    return "Vec<" + typeToString(t->inner) + ">";
        case SemKind::Struct: return "<struct>";
        case SemKind::Error:  return "<error>";
    }
    return "<error>";
}
