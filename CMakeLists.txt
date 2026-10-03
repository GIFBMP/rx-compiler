cmake_minimum_required(VERSION 3.15)

project(rx-compiler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(grammar)

add_executable(rxc
    src/main.cpp
    src/AST/AstBuilder.cpp
)

target_include_directories(rxc PRIVATE src)

target_link_libraries(rxc PRIVATE RxGrammar)
