// Solis Programming Language - Main Entry Point
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "cli/compiler/compiler.hpp"
#include "cli/interpreter/interpreter.hpp"
#include "cli/lsp/lsp.hpp"
#include "cli/module/module_resolver.hpp"
#include "cli/module/namespace_manager.hpp"
#include "cli/repl/repl.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "type/typer.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

// ANSI color codes for improved error messages
const char* RED = "\033[31m";
const char* YELLOW = "\033[33m";
const char* GREEN = "\033[32m";
const char* CYAN = "\033[36m";
const char* BOLD = "\033[1m";
const char* RESET = "\033[0m";

// Track loaded modules to prevent circular imports
std::set<std::string> loadedModules;

// Find module file in configured search paths
// Converts module name to file path and searches in priority order
std::optional<std::string> findModuleFile(const std::string& moduleName,
                                          const std::string& currentDir) {
  // Convert module name to file path
  // "Data.List" -> "Data/List.solis"
  // "MathUtils" -> "MathUtils.solis"
  std::string filePath = moduleName;
  std::replace(filePath.begin(), filePath.end(), '.', '/');
  filePath += ".solis";

  // Search paths in order:
  // 1. src/solis/std/ directory (standard library)
  // 2. Current directory (relative to current file)
  // 3. Current working directory
  // 4. src/solis/prelude directory (for backwards compatibility)
  std::vector<std::string> searchPaths = {
      "src/solis/std/" + filePath,     // e.g., src/solis/std/Data/List.solis
      currentDir + "/" + filePath,     // e.g., ./Data/List.solis
      filePath,                        // e.g., MathUtils.solis
      "src/solis/prelude/" + filePath  // e.g., src/solis/prelude/MathUtils.solis
  };

  for (const auto& path : searchPaths) {
    if (std::filesystem::exists(path)) {
      return std::filesystem::canonical(path).string();
    }
  }

  return std::nullopt;
}

// Load module from file system and return its declarations
// Handles circular import detection and recursive import processing
std::vector<solis::DeclPtr> loadModule(const std::string& moduleName,
                                       const std::string& currentDir,
                                       solis::Interpreter& interp) {
  // Check for circular imports
  if (loadedModules.count(moduleName)) {
    return {};  // Already loaded
  }

  // Find module file
  auto modulePathOpt = findModuleFile(moduleName, currentDir);
  if (!modulePathOpt) {
    std::cerr << RED << "Error:" << RESET << " Module not found: " << moduleName << std::endl;
    return {};
  }

  std::string modulePath = *modulePathOpt;
  loadedModules.insert(moduleName);

  // Read module file
  std::ifstream file(modulePath);
  if (!file) {
    std::cerr << RED << "Error:" << RESET << " Could not open module file: " << modulePath
              << std::endl;
    return {};
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string source = buffer.str();

  // Parse module
  solis::Lexer lexer(source);
  auto tokens = lexer.tokenize();
  solis::Parser parser(std::move(tokens));
  auto module = parser.parseModule();

  // Get directory of this module for nested imports
  std::string moduleDir = std::filesystem::path(modulePath).parent_path().string();

  // Process imports recursively
  for (const auto& import : module.imports) {
    auto importedDecls = loadModule(import.moduleName, moduleDir, interp);
    for (auto& decl : importedDecls) {
      interp.evalAndStore(std::move(decl));
    }
  }

  // Evaluate and store this module's declarations
  for (auto& decl : module.declarations) {
    if (decl) {
      interp.evalAndStore(std::move(decl));
    }
  }

  return std::move(module.declarations);
}

// Load Prelude definitions into interpreter and type environment
// Performs two-pass loading: runtime evaluation and type inference
void loadPrelude(solis::Interpreter& interp, solis::TypeInference& typer) {
  std::ifstream file("src/solis/prelude/prelude.solis");
  if (!file.is_open()) {
    std::cerr << "Warning: Could not open src/solis/prelude/prelude.solis" << std::endl;
    return;
  }

  std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

  try {
    // Evaluate declarations for runtime
    solis::Lexer lexer(source);
    solis::Parser parser(lexer.tokenize());
    auto module = parser.parseModule();

    for (auto& decl : module.declarations) {
      interp.evalAndStore(std::move(decl));
    }

    // Second pass: type inference for all functions
    solis::Lexer lexer2(source);
    solis::Parser parser2(lexer2.tokenize());
    auto module2 = parser2.parseModule();

    int successCount = 0;
    for (const auto& decl : module2.declarations) {
      if (!decl)
        continue;

      try {
        auto result = typer.inferDecl(*decl);

        // Store in type environment
        if (auto* funcDecl = std::get_if<solis::FunctionDecl>(&decl->node)) {
          auto finalType = result.subst.apply(result.type);
          if (!result.constraints.empty()) {
            finalType = tyQual(result.constraints, finalType);
          }
          auto typeScheme = typer.env().generalize(finalType);
          auto newEnv = typer.env();
          newEnv.extend(funcDecl->name, typeScheme);
          typer.setEnv(newEnv);
          successCount++;
        }
      } catch (...) {
        // Skip functions that fail type inference
      }
    }

    std::cout << "Prelude loaded (" << successCount << " functions type-checked)" << std::endl;

  } catch (const std::exception& e) {
    std::cerr << "Error loading prelude: " << e.what() << std::endl;
  }
}

// Start interactive REPL session
void runRepl() {
  solis::Interpreter interpreter;

  // Create REPL with typer
  solis::REPL repl(interpreter);
  repl.initialize();

  // Load prelude with type inference
  loadPrelude(interpreter, repl.context().typechecker());
  repl.run();
}

// Execute Solis source file
// Loads prelude, processes imports, and executes main function
int runFile(const std::string& filename) {
  solis::Interpreter interpreter;

  // Initialize module system
  auto moduleResolver = std::make_shared<solis::ModuleResolver>();
  auto namespaceManager = std::make_shared<solis::NamespaceManager>();
  interpreter.setModuleResolver(moduleResolver);
  interpreter.setNamespaceManager(namespaceManager);

  // Set current directory for module resolution
  std::string currentDir = std::filesystem::path(filename).parent_path().string();
  if (currentDir.empty())
    currentDir = ".";
  interpreter.setCurrentDirectory(currentDir);

  // Eagerly scan for smart import suggestions
  auto searchPaths = moduleResolver->getSearchPaths(currentDir);
  namespaceManager->scanAvailableModules(searchPaths);

  // Typer required for loadPrelude
  solis::TypeInference typer(solis::TypeEnv::builtins());
  loadPrelude(interpreter, typer);

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

    // Two-phase module loading for forward references
    // Phase 1: Register all function names
    for (const auto& decl : module.declarations) {
      if (std::holds_alternative<solis::FunctionDecl>(decl->node)) {
        const auto& func = std::get<solis::FunctionDecl>(decl->node);
        interpreter.addBinding(func.name, std::make_shared<solis::Value>());
      }
    }

    // Phase 2: Evaluate all function bodies
    for (auto& decl : module.declarations) {
      if (decl) {
        interpreter.evalAndStore(std::move(decl));
      }
    }

    // Clear loaded modules tracker for this file
    loadedModules.clear();

    // Get directory of current file for import resolution
    std::string currentDir = std::filesystem::path(filename).parent_path().string();
    if (currentDir.empty())
      currentDir = ".";

    // Process imports
    for (const auto& importDecl : module.imports) {
      solis::Decl decl;
      decl.node = importDecl;
      interpreter.eval(decl);
    }

    // Execute main
    auto mainExpr = std::make_unique<solis::Expr>(solis::Expr{solis::Var{"main"}});
    try {
      auto result = interpreter.eval(*mainExpr);
      // Main executed successfully
    } catch (const std::exception& e) {
      std::cerr << RED << "Runtime Error:" << RESET << " " << e.what() << std::endl;
      return 1;
    } catch (...) {
      std::cerr << RED << "Runtime Error:" << RESET << " Unknown error" << std::endl;
      return 1;
    }
  } catch (const std::exception& e) {
    std::cerr << RED << "Error:" << RESET << " " << e.what() << std::endl;
    return 1;
  }

  return 0;
}

// Main entry point for Solis compiler/interpreter
// Handles command-line arguments and routes to appropriate subcommands
int main(int argc, char* argv[]) {
  if (argc < 2) {
    runRepl();
  } else if (std::string(argv[1]) == "repl") {
    runRepl();
  } else if (std::string(argv[1]) == "run" && argc >= 3) {
    runFile(argv[2]);
  } else if (std::string(argv[1]) == "compile" && argc >= 3) {
    solis::compileFile(argv[2]);
  } else if (std::string(argv[1]) == "lsp") {
    solis::lsp::LanguageServer server;
    server.run();
  } else {
    std::cout << BOLD << "Usage:" << RESET << std::endl;
    std::cout << "  " << argv[0] << " " << GREEN << "repl" << RESET
              << "          - Start interactive REPL" << std::endl;
    std::cout << "  " << argv[0] << " " << GREEN << "run" << RESET
              << " FILE      - Execute a Solis file" << std::endl;
    std::cout << "  " << argv[0] << " " << GREEN << "compile" << RESET
              << " FILE  - Compile a Solis file to native executable" << std::endl;
    std::cout << "  " << argv[0] << " " << GREEN << "lsp" << RESET
              << "           - Start LSP server" << std::endl;
  }

  return 0;
}
