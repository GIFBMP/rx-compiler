#include "LLVMEmit.h"

#include <iostream>
#include <string>

namespace {

std::string typeName(const IRType *type) {
    if(type == nullptr) return "void";

    switch(type->kind) {
        case IRKind::Void: return "void";
        case IRKind::I1: return "i1";
        case IRKind::I8: return "i8";
        case IRKind::I32: return "i32";
        case IRKind::Ptr: return "ptr";
        case IRKind::Array:
            return "[" + std::to_string(type->arrayLen) + " x " + typeName(type->elem) + "]";
        case IRKind::Struct:
            return type->structInfo ? "%struct." + type->structInfo->name : "%struct";
    }
    return "void";
}

std::string valueName(const IRValue &value) {
    switch(value.kind) {
        case IRValueKind::Reg: return "%r" + std::to_string(value.id);
        case IRValueKind::Int: return std::to_string(value.intValue);
        case IRValueKind::Null: return "null";
        case IRValueKind::Global:
            return value.global ? "@" + value.global->name : "null";
    }
    return "null";
}

std::string blockLabel(size_t index) {
    return index == 0 ? "entry" : "bb" + std::to_string(index);
}

bool emitInstr(const IRInstr &instr, std::ostream &out) {
    switch(instr.op) {
        case IROp::Ret: {
            if(instr.args.empty()) {
                out << "  ret void\n";
            }
            else {
                out << "  ret " << typeName(instr.args[0].type) << " "
                    << valueName(instr.args[0]) << "\n";
            }
            return true;
        }
        case IROp::Unreachable: {
            out << "  unreachable\n";
            return true;
        }
        case IROp::Alloca: {
            out << "  " << valueName(instr.dst) << " = alloca "
                << typeName(instr.type);
            if(instr.align != 0) {
                out << ", align " << instr.align;
            }
            out << "\n";
            return true;
        }
        case IROp::Load: {
            if(!instr.hasDst || instr.args.empty()) return false;
            out << "  " << valueName(instr.dst) << " = load "
                << typeName(instr.type) << ", ptr "
                << valueName(instr.args[0]) << "\n";
            return true;
        }
        case IROp::Store: {
            if(instr.args.size() < 2) return false;
            out << "  store " << typeName(instr.args[0].type) << " "
                << valueName(instr.args[0]) << ", ptr "
                << valueName(instr.args[1]) << "\n";
            return true;
        }
        default: {
            std::cerr << "LLVMEmit: unsupported instruction\n";
            return false;
        }
    }
}

} // namespace

bool LLVMEmit::emit(const IRModule &module, std::ostream &out) {
    out << "target triple = \"riscv32-unknown-none-elf\"\n\n";

    for(const auto &funcPtr : module.funcs) {
        const IRFunc &func = *funcPtr;

        out << "define " << typeName(func.retType) << " @" << func.name << "(";
        for(size_t i = 0; i < func.params.size(); i++) {
            if(i != 0) out << ", ";
            const IRValue &param = func.params[i];
            out << typeName(param.type) << " " << valueName(param);
        }
        out << ") {\n";

        for(size_t i = 0; i < func.blocks.size(); i++) {
            const IRBlock &block = *func.blocks[i];
            out << blockLabel(i) << ":\n";
            for(const auto &instr : block.instrs) {
                if(!emitInstr(*instr, out)) {
                    return false;
                }
            }
        }

        out << "}\n\n";
    }

    return true;
}
