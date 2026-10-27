#pragma once

#include "IR.h"

#include <iosfwd>

class LLVMEmit {
public:
    bool emit(const IRModule &module, std::ostream &out);
};
