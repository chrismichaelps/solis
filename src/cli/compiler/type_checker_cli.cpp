// Solis Programming Language - Type Checker CLI
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "type/typer.hpp"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace solis;

// Main entry point for type checker CLI
// Analyzes Solis source files and displays type inference results
int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <file.solis>" << std::endl;
    return 1;
  }

  std::ifstream file(argv[1]);
  if (!file.is_open()) {
    std::cerr << "Error: Could not open file " << argv[1] << std::endl;
    return 1;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  try {
    Lexer lexer(source);
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto module = parser.parseModule();

    TypeEnv env = TypeEnv::builtins();
    TypeInference typer(env);

    std::cout << "Type Inference Results:\\n";
    std::cout << std::string(60, '=') << "\\n\\n";

    for (const auto& decl : module.declarations) {
      if (auto* func = std::get_if<FunctionDecl>(&decl->node)) {
        try {
          auto result = typer.inferDecl(*decl);
          auto scheme = typer.env().lookup(func->name);

          std::cout << "\\033[1;36m" << func->name << "\\033[0m :: ";
          std::cout << "\\033[1;32m" << scheme.toString() << "\\033[0m\\n";

        } catch (const SolisError& e) {
          std::cout << "\\033[1;36m" << func->name << "\\033[0m :: ";
          std::cout << "\\033[1;31mERROR: " << e.what() << "\\033[0m\\n";
        }
      }
    }

    std::cout << "\\n" << std::string(60, '=') << "\\n";
    std::cout << "Total functions analyzed: " << module.declarations.size() << "\\n";

  } catch (const std::exception& e) {
    std::cerr << "\\033[1;31mError:\\033[0m " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
