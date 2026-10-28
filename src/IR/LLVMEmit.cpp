#include "LLVMEmit.h"

#include <iostream>
#include <set>
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

std::string escapeBytes(const std::vector<unsigned char> &bytes) {
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for(unsigned char byte : bytes) {
        out.push_back('\\');
        out.push_back(hex[(byte >> 4) & 0xF]);
        out.push_back(hex[byte & 0xF]);
    }
    return out;
}

std::string blockLabel(size_t index) {
    return index == 0 ? "entry" : "bb" + std::to_string(index);
}

const char *binaryOpName(IROp op) {
    switch(op) {
        case IROp::Add:  return "add";
        case IROp::Sub:  return "sub";
        case IROp::Mul:  return "mul";
        case IROp::SDiv: return "sdiv";
        case IROp::UDiv: return "udiv";
        case IROp::SRem: return "srem";
        case IROp::URem: return "urem";
        case IROp::And:  return "and";
        case IROp::Or:   return "or";
        case IROp::Xor:  return "xor";
        case IROp::Shl:  return "shl";
        case IROp::LShr: return "lshr";
        case IROp::AShr: return "ashr";
        default: return nullptr;
    }
}

const char *icmpPredicate(IRCmp cmp) {
    switch(cmp) {
        case IRCmp::Eq:  return "eq";
        case IRCmp::Ne:  return "ne";
        case IRCmp::Ugt: return "ugt";
        case IRCmp::Uge: return "uge";
        case IRCmp::Ult: return "ult";
        case IRCmp::Ule: return "ule";
        case IRCmp::Sgt: return "sgt";
        case IRCmp::Sge: return "sge";
        case IRCmp::Slt: return "slt";
        case IRCmp::Sle: return "sle";
    }
    return "eq";
}

void collectStructs(const IRType *type,
                    std::set<const IRStruct *> &seen,
                    std::vector<const IRStruct *> &out) {
    if(type == nullptr) return;

    if(type->kind == IRKind::Array) {
        collectStructs(type->elem, seen, out);
        return;
    }
    if(type->kind != IRKind::Struct || type->structInfo == nullptr) {
        return;
    }

    const IRStruct *info = type->structInfo;
    if(!seen.insert(info).second) return;

    for(const auto &field : info->fields) {
        collectStructs(field.type, seen, out);
    }
    out.push_back(info);
}

void collectStructs(const IRValue &value,
                    std::set<const IRStruct *> &seen,
                    std::vector<const IRStruct *> &out) {
    collectStructs(value.type, seen, out);
}

std::vector<const IRStruct *> moduleStructs(const IRModule &module) {
    std::set<const IRStruct *> seen;
    std::vector<const IRStruct *> out;

    for(const auto &global : module.globals) {
        collectStructs(global->type, seen, out);
    }

    for(const auto &func : module.funcs) {
        collectStructs(func->retType, seen, out);
        for(const auto &param : func->params) {
            collectStructs(param, seen, out);
        }
        for(const auto &block : func->blocks) {
            for(const auto &instr : block->instrs) {
                collectStructs(instr->type, seen, out);
                collectStructs(instr->dst, seen, out);
                for(const auto &arg : instr->args) {
                    collectStructs(arg, seen, out);
                }
            }
        }
    }

    return out;
}

bool usesOp(const IRModule &module, IROp op) {
    for(const auto &func : module.funcs) {
        for(const auto &block : func->blocks) {
            for(const auto &instr : block->instrs) {
                if(instr->op == op) return true;
            }
        }
    }
    return false;
}

std::string callTarget(const IRInstr &instr) {
    return instr.calleeName;
}

size_t varArgFixedCount(const IRInstr &instr) {
    return instr.args.empty() ? 0 : 1;
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
        case IROp::Br: {
            out << "  br label %" << blockLabel(instr.thenBrBlock) << "\n";
            return true;
        }
        case IROp::CondBr: {
            if(instr.args.empty()) return false;
            out << "  br i1 " << valueName(instr.args[0])
                << ", label %" << blockLabel(instr.thenBrBlock)
                << ", label %" << blockLabel(instr.elseBrBlock) << "\n";
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
        case IROp::Gep: {
            if(!instr.hasDst || instr.args.empty()) return false;

            out << "  " << valueName(instr.dst) << " = getelementptr "
                << typeName(instr.type) << ", ptr "
                << valueName(instr.args[0]);
            for(size_t i = 1; i < instr.args.size(); i++) {
                out << ", " << typeName(instr.args[i].type) << " "
                    << valueName(instr.args[i]);
            }
            out << "\n";
            return true;
        }
        case IROp::Add:
        case IROp::Sub:
        case IROp::Mul:
        case IROp::SDiv:
        case IROp::UDiv:
        case IROp::SRem:
        case IROp::URem:
        case IROp::And:
        case IROp::Or:
        case IROp::Xor:
        case IROp::Shl:
        case IROp::LShr:
        case IROp::AShr: {
            if(!instr.hasDst || instr.args.size() < 2) return false;
            const char *opName = binaryOpName(instr.op);
            if(opName == nullptr) return false;

            out << "  " << valueName(instr.dst) << " = " << opName << " "
                << typeName(instr.type) << " "
                << valueName(instr.args[0]) << ", "
                << valueName(instr.args[1]) << "\n";
            return true;
        }
        case IROp::ICmp: {
            if(!instr.hasDst || instr.args.size() < 2) return false;

            out << "  " << valueName(instr.dst) << " = icmp "
                << icmpPredicate(instr.cmp) << " "
                << typeName(instr.args[0].type) << " "
                << valueName(instr.args[0]) << ", "
                << valueName(instr.args[1]) << "\n";
            return true;
        }
        case IROp::Call: {
            std::string target = callTarget(instr);
            if(target.empty()) return false;

            if(instr.hasDst) {
                out << "  " << valueName(instr.dst) << " = ";
            }
            else {
                out << "  ";
            }
            out << "call " << typeName(instr.type) << " ";

            if(instr.varArg) {
                out << "(";
                size_t fixed = varArgFixedCount(instr);
                for(size_t i = 0; i < fixed; i++) {
                    if(i != 0) out << ", ";
                    out << typeName(instr.args[i].type);
                }
                if(fixed != 0) out << ", ";
                out << "...) ";
            }

            out << "@" << target << "(";
            for(size_t i = 0; i < instr.args.size(); i++) {
                if(i != 0) out << ", ";
                out << typeName(instr.args[i].type) << " "
                    << valueName(instr.args[i]);
            }
            out << ")\n";
            return true;
        }
        case IROp::Memcpy:
        case IROp::Memmove: {
            if(instr.args.size() < 3) return false;
            const char *name = instr.op == IROp::Memcpy
                ? "llvm.memcpy.p0.p0.i32"
                : "llvm.memmove.p0.p0.i32";
            out << "  call void @" << name << "(ptr "
                << valueName(instr.args[0]) << ", ptr "
                << valueName(instr.args[1]) << ", i32 "
                << valueName(instr.args[2]) << ", i1 false)\n";
            return true;
        }
        default: {
            std::cerr << "LLVMEmit: unsupported instruction\n";
            return false;
        }
    }
}

std::vector<std::string> externalDecls(const IRModule &module) {
    std::set<std::string> defined;
    for(const auto &func : module.funcs) {
        defined.insert(func->name);
    }

    std::set<std::string> seen;
    std::vector<std::string> decls;

    for(const auto &func : module.funcs) {
        for(const auto &block : func->blocks) {
            for(const auto &instr : block->instrs) {
                if(instr->op != IROp::Call) continue;

                std::string target = callTarget(*instr);
                if(target.empty() || defined.count(target) != 0) continue;
                if(!seen.insert(target).second) continue;

                size_t fixed = instr->varArg
                    ? varArgFixedCount(*instr)
                    : instr->args.size();

                std::string decl = "declare " + typeName(instr->type)
                    + " @" + target + "(";
                for(size_t i = 0; i < fixed && i < instr->args.size(); i++) {
                    if(i != 0) decl += ", ";
                    decl += typeName(instr->args[i].type);
                }
                if(instr->varArg) {
                    if(fixed != 0) decl += ", ";
                    decl += "...";
                }
                decl += ")";
                decls.push_back(std::move(decl));
            }
        }
    }

    return decls;
}

} // namespace

bool LLVMEmit::emit(const IRModule &module, std::ostream &out) {
    out << "target triple = \"riscv32-unknown-none-elf\"\n";

    for(const auto *info : moduleStructs(module)) {
        out << "%struct." << info->name << " = type { ";
        for(size_t i = 0; i < info->fields.size(); i++) {
            if(i != 0) out << ", ";
            out << typeName(info->fields[i].type);
        }
        out << " }\n";
    }

    for(const auto &global : module.globals) {
        out << "@" << global->name << " = private unnamed_addr constant "
            << typeName(global->type) << " c\""
            << escapeBytes(global->bytes) << "\"";
        if(global->align != 0) {
            out << ", align " << global->align;
        }
        out << "\n";
    }

    if(usesOp(module, IROp::Memcpy)) {
        out << "declare void @llvm.memcpy.p0.p0.i32(ptr, ptr, i32, i1)\n";
    }
    if(usesOp(module, IROp::Memmove)) {
        out << "declare void @llvm.memmove.p0.p0.i32(ptr, ptr, i32, i1)\n";
    }
    for(const auto &decl : externalDecls(module)) {
        out << decl << "\n";
    }
    out << "\n";

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
