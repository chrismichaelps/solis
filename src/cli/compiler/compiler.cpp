// Solis Programming Language - Compiler Driver
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/compiler/compiler.hpp"

#include "codegen/codegen.hpp"
#include "codegen/support/diagnostics.hpp"
#include "parser/ast.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "type/typer.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

// ANSI color codes
static const char* RED = "\033[31m";
static const char* GREEN = "\033[32m";
static const char* RESET = "\033[0m";

namespace solis {

// Compile Solis source file to native executable
// Performs lexing, parsing, type inference, and code generation
int compileFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << RED << "Error:" << RESET << " Could not open file " << filename << std::endl;
    return 1;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  try {
    solis::Lexer lexer(source);
    auto tokens = lexer.tokenize();

    solis::Parser parser(std::move(tokens));
    auto module = parser.parseModule();

    // Type inference
    solis::TypeInference typer(solis::TypeEnv::builtins());

    // Load prelude types
    std::ifstream preludeFile("src/solis/prelude/prelude.solis");
    if (preludeFile.is_open()) {
      std::string preludeSource((std::istreambuf_iterator<char>(preludeFile)),
                                std::istreambuf_iterator<char>());
      solis::Lexer preludeLexer(preludeSource);
      solis::Parser preludeParser(preludeLexer.tokenize());
      auto preludeModule = preludeParser.parseModule();

      for (const auto& decl : preludeModule.declarations) {
        if (!decl)
          continue;
        try {
          auto result = typer.inferDecl(*decl);
          if (auto* funcDecl = std::get_if<solis::FunctionDecl>(&decl->node)) {
            auto finalType = result.subst.apply(result.type);
            if (!result.constraints.empty()) {
              finalType = tyQual(result.constraints, finalType);
            }
            auto typeScheme = typer.env().generalize(finalType);
            auto newEnv = typer.env();
            newEnv.extend(funcDecl->name, typeScheme);
            typer.setEnv(newEnv);
          }
        } catch (...) {
        }
      }
    }

    // Infer types for current module
    for (const auto& decl : module.declarations) {
      if (!decl)
        continue;
      try {
        typer.inferDecl(*decl);
      } catch (const std::exception& e) {
        std::cerr << RED << "Type Error:" << RESET << " " << e.what() << std::endl;
        return 1;
      }
    }

    // Code generation
    std::string moduleName = std::filesystem::path(filename).stem().string();
    solis::DiagnosticEngine diags;
    solis::CodeGen codegen(moduleName, diags);
    codegen.setTypeInference(&typer);

    codegen.compileModule(module);

    // Check for code generation errors
    if (diags.hasErrors()) {
      std::cerr << RED << "Code generation failed with " << diags.getErrorCount() << " error(s)"
                << RESET << std::endl;
      return 1;
    }

    // Emit executable
    std::string exeName = moduleName;
    codegen.emitExecutable(exeName);

    std::cout << GREEN << "Successfully compiled " << filename << " to " << exeName << RESET
              << std::endl;
  } catch (const std::exception& e) {
    std::cerr << RED << "Error:" << RESET << " " << e.what() << std::endl;
    return 1;
  }

  return 0;
}

}  // namespace solis
