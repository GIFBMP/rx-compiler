#pragma once
#include <cassert>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace rx::ir {

enum class IRKind {
    Void, 
    I1, //Bool
    I8, //to process print_i32/println_i32/get_i32->format string
    I32, //Integer
    Ptr,
    Array,
    Struct,
};

struct IRStruct;

struct IRType {
    IRKind kind = IRKind::Void;
    IRType *elem = nullptr;
    size_t arrayLen = 0;
    IRStruct *structInfo = nullptr;
    size_t size = 0;
    size_t align = 1;
};

struct IRField {
    std::string name;
    IRType *type = nullptr;
    size_t offset = 0;
};

struct IRStruct {
    std::string name;
    std::vector<IRField> fields;
    size_t size = 0;
    size_t align = 1;
};

inline size_t irAlignTo(size_t val, size_t align) {
    return (val + align - 1) / align * align;
}

class TypeContext {
public:
    IRType *voidType() {
        if(voidType_ == nullptr) voidType_ = make(IRKind::Void, 0, 1);
        return voidType_;
    }

    IRType *i1() {
        if(i1_ == nullptr) i1_ = make(IRKind::I1, 1, 1);
        return i1_;
    }

    IRType *i8() {
        if(i8_ == nullptr) i8_ = make(IRKind::I8, 1, 1);
        return i8_;
    }

    IRType *i32() {
        if(i32_ == nullptr) i32_ = make(IRKind::I32, 4, 4);
        return i32_;
    }

    IRType *ptr() {
        if(ptr_ == nullptr) ptr_ = make(IRKind::Ptr, 4, 4);
        return ptr_;
    }

    IRType *array(IRType *elem, size_t len) {
        auto key = std::make_pair(elem, len);
        auto it = arrays.find(key);
        if(it != arrays.end()) return it->second;
        IRType *type = make(IRKind::Array, elem->size * len, elem->align);
        type->elem = elem;
        type->arrayLen = len;
        arrays.emplace(key, type);
        return type;
    }

    IRType *structType(IRStruct *info) {
        auto it = structs.find(info);
        if(it != structs.end()) return it->second;
        IRType *type = make(IRKind::Struct, info->size, info->align);
        type->structInfo = info;
        structs.emplace(info, type);
        return type;
    }

    IRStruct *makeStruct(const std::string &name, const std::vector<IRField> &fields) {
        auto info = std::make_unique<IRStruct>();
        info->name = name;
        info->fields = fields;
        size_t offset = 0, align = 1;
        for(auto &field : info->fields) {
            offset = irAlignTo(offset, field.type->align);
            field.offset = offset;
            offset += field.type->size;
            if(field.type->align > align) align = field.type->align;
        }
        info->size = irAlignTo(offset, align);
        info->align = align;
        IRStruct *raw = info.get();
        ownedStructs.push_back(std::move(info));
        return raw;
    }

    IRStruct *makeStruct(const std::string &name, const std::vector<IRField> &fields,
                         const std::vector<size_t> &offsets, size_t size, size_t align) {
        assert(fields.size() == offsets.size());
        assert(align > 0);
        auto info = std::make_unique<IRStruct>();
        info->name = name;
        info->fields = fields;
        for(size_t i = 0; i < info->fields.size() && i < offsets.size(); i++) {
            info->fields[i].offset = offsets[i];
        }
        info->size = size;
        info->align = align;
        IRStruct *raw = info.get();
        ownedStructs.push_back(std::move(info));
        return raw;
    }

    IRType *vecHeader() {
        if(vecType_ == nullptr) {
            std::vector<IRField> fields = {
                {"data", ptr(), 0},
                {"len", i32(), 0},
                {"cap", i32(), 0},
            };
            vecStruct_ = makeStruct("Vec", fields);
            vecType_ = structType(vecStruct_);
        }
        return vecType_;
    }

private:
    IRType *make(IRKind kind, size_t size, size_t align) {
        auto type = std::make_unique<IRType>();
        type->kind = kind;
        type->size = size;
        type->align = align;
        IRType *raw = type.get();
        ownedTypes.push_back(std::move(type));
        return raw;
    }

    std::vector<std::unique_ptr<IRType>> ownedTypes;
    std::vector<std::unique_ptr<IRStruct>> ownedStructs;
    std::map<std::pair<IRType *, size_t>, IRType *> arrays;
    std::unordered_map<IRStruct *, IRType *> structs;

    IRType *voidType_ = nullptr;
    IRType *i1_ = nullptr;
    IRType *i8_ = nullptr;
    IRType *i32_ = nullptr;
    IRType *ptr_ = nullptr;
    IRStruct *vecStruct_ = nullptr;
    IRType *vecType_ = nullptr;
};

enum class IRValueKind {
    Reg, Int, Null, Global,
};

struct IRGlobal;
struct IRValue {//imm/reg/... (operand)
    IRValueKind kind = IRValueKind::Reg;
    IRType *type = nullptr;
    size_t id = 0;//Reg
    long long intValue = 0;//Int
    IRGlobal *global = nullptr;//Global
};

enum class IROp {
    Alloca, Load, Store, Gep,
    Add, Sub, Mul, SDiv, UDiv, SRem, URem,
    And, Or, Xor, Shl, LShr, AShr,
    ICmp, Zext, Trunc, Bitcast, PtrToInt, IntToPtr,
    Call, Br, CondBr, Ret, Unreachable, Memcpy, Memmove,
};

enum class IRCmp {
    //U:unsigned cmp
    //S:signed cmp
    Eq, Ne, Ugt, Uge, Ult, Ule, Sgt, Sge, Slt, Sle,
};

struct IRFunc;

struct IRInstr {
    IROp op;
    IRType *type = nullptr; 
    IRValue dst;
    bool hasDst = false;
    std::vector<IRValue> args;

    IRCmp cmp = IRCmp::Eq;//for icmp instruction

    size_t thenBrBlock = 0;//br
    size_t elseBrBlock = 0;//cond br

    std::string calleeName;
    bool varArg = false;
    size_t align = 0;
};

struct IRBlock {
    std::string name;
    std::vector<std::unique_ptr<IRInstr>> instrs;
};

struct IRFunc {
    std::string name;
    IRType *retType = nullptr;
    std::vector<IRValue> params;
    std::vector<std::unique_ptr<IRBlock>> blocks;
};

struct IRGlobal {//string format Const(println!...)
    std::string name;
    IRType *type = nullptr;
    std::vector<unsigned char> bytes;
    size_t align = 1;
};

struct IRModule {
    std::vector<std::unique_ptr<IRFunc>> funcs;
    std::vector<std::unique_ptr<IRGlobal>> globals;
};
} // namespace rx::ir
