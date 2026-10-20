#pragma once

enum class IRType {
    Void, 
    I1, //Bool
    I8, //to process print_i32/println_i32/get_i32->format string
    I32, //Integer
    Ptr,
    Array,
    Struct,
};