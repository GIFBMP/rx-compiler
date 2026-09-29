cmake_minimum_required(VERSION 3.15)

project(RxGrammar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ANTLR 4 C++ runtime (headers live in an antlr4-runtime/ subdirectory).
find_path(ANTLR4_INCLUDE_DIR
    NAMES antlr4-runtime.h
    PATH_SUFFIXES antlr4-runtime
)
find_library(ANTLR4_LIBRARY NAMES antlr4-runtime)

if(NOT ANTLR4_INCLUDE_DIR OR NOT ANTLR4_LIBRARY)
    message(FATAL_ERROR
        "ANTLR4 C++ runtime not found. Install it, e.g. 'apt install libantlr4-runtime-dev', "
        "or set ANTLR4_INCLUDE_DIR and ANTLR4_LIBRARY.")
endif()

find_package(Threads REQUIRED)

# Generated from RxLexer.g4 / RxParser.g4. To regenerate after editing the
# grammar (note the names must not be Lexer/Parser or they collide with
# antlr4::Lexer/antlr4::Parser in the generated C++):
#   java -jar <antlr-4.13.x-complete.jar> -Dlanguage=Cpp -visitor -no-listener \
#       RxLexer.g4 RxParser.g4
set(RX_GRAMMAR_SOURCES
    RxLexer.cpp
    RxParser.cpp
    RxParserBaseVisitor.cpp
    RxParserVisitor.cpp
)

set(RX_GRAMMAR_HEADERS
    RxLexer.h
    RxParser.h
    RxParserBaseVisitor.h
    RxParserVisitor.h
)

add_library(RxGrammar STATIC ${RX_GRAMMAR_SOURCES} ${RX_GRAMMAR_HEADERS})

target_include_directories(RxGrammar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ANTLR4_INCLUDE_DIR}
)

target_link_libraries(RxGrammar
    PUBLIC
        ${ANTLR4_LIBRARY}
        Threads::Threads
)
