#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "antlr4-runtime.h"
#include "RxLexer.h"
#include "RxParser.h"
#include "AST/AstBuilder.h"
#include "semantic/analyzer.h"

static std::string readAll(std::istream &in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main(int argc, char **argv) {
    std::string entry = "crate";
    std::string stage;
    std::string path;
    bool checkMode = false;

    for(int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if(arg == "--entry" && i + 1 < argc) {
            entry = argv[++i];
        }
        else if(arg == "--stage" && i + 1 < argc) {
            stage = argv[++i];
        }
        else if(arg == "--check") {
            checkMode = true;
        }
        else {
            path = arg;
        }
    }

    std::string src;
    if(!path.empty()) {
        std::ifstream file(path, std::ios::binary);
        if(!file) {
            std::cerr << "cannot open: " << path << "\n";
            return 1;
        }
        src = readAll(file);
    }
    else {
        src = readAll(std::cin);
    }

    NodePtr<AstNode> ast = AstBuilder::parseEntry(src, stage.empty() ? entry : "crate");
    if(!ast) {
        std::cerr << "parse/build failed (entry=" << entry << ")\n";
        return 1;
    }

    if(!stage.empty()) {
        if(stage == "parse") {
            return 0;
        }
        auto *crate = dynamic_cast<Crate*>(ast.get());
        if(crate == nullptr) {
            std::cerr << "semantic stage requires a crate entry\n";
            return 1;
        }
        SemanticAnalyzer analyzer;
        bool ok = analyzer.analyze(crate);
        for(const Diagnostic &d : analyzer.diagnostics()) {
            std::cerr << d.span.beginLine << ":" << d.span.beginCol
                        << ": error: " << d.message << "\n";
        }
        if(stage == "semantic") {
            return ok ? 0 : 1;
        }
        if(stage == "ir") {
            if(ok == false) {
                std::cerr << "invalid semantic\n";
                return 1;
            }
            
        }
        std::cerr << "unsupported stage: " << stage << "\n";
        return 1;
    }

    return 0;
}
