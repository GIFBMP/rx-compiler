#pragma once

#include "IR.h"

#include <iosfwd>
namespace rx::ir {

class LLVMEmit {
public:
    bool emit(const IRModule &module, std::ostream &out);
};
} // namespace rx::ir
