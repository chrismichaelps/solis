// Solis Programming Language - Code Generation CLI
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/codegen.hpp"
#include "codegen/support/diagnostics.hpp"
#include "parser/lexer.hpp"
#include "parser/parser.hpp"
#include "type/typer.hpp"

#include <llvm/IR/Verifier.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace solis;

// Print command-line usage information for the codegen compiler
void printUsage(const char* progName) {
  std::cout << "Usage: " << progName << " compile <input.solis> [options]\n";
  std::cout << "\nOptions:\n";
  std::cout << "  -o <output>        Output file (default: a.out)\n";
  std::cout << "  --emit-llvm        Emit LLVM IR (.ll file)\n";
  std::cout << "  --emit-asm         Emit assembly (.s file)\n";
  std::cout << "  --emit-obj         Emit object file (.o)\n";
  std::cout << "  -O<level>          Optimization level (0-3, default: 2)\n";
  std::cout << "  --help             Show this help message\n";
  std::cout << "\nExamples:\n";
  std::cout << "  " << progName << " compile hello.solis -o hello\n";
  std::cout << "  " << progName << " compile fib.solis --emit-llvm -o fib.ll\n";
  std::cout << "  " << progName << " compile program.solis -O3 -o optimized\n";
}

struct CompileOptions {
  std::string inputFile;
  std::string outputFile = "a.out";
  bool emitLLVM = false;
  bool emitAsm = false;
  bool emitObj = false;
  int optLevel = 2;
};

// Parse command-line arguments into CompileOptions structure
// Validates arguments and exits on error
CompileOptions parseArgs(int argc, char* argv[]) {
  CompileOptions opts;

  if (argc < 3) {
    printUsage(argv[0]);
    exit(1);
  }

  opts.inputFile = argv[2];

  for (int i = 3; i < argc; i++) {
    std::string arg = argv[i];

    if (arg == "--help") {
      printUsage(argv[0]);
      exit(0);
    } else if (arg == "-o" && i + 1 < argc) {
      opts.outputFile = argv[++i];
    } else if (arg == "--emit-llvm") {
      opts.emitLLVM = true;
    } else if (arg == "--emit-asm") {
      opts.emitAsm = true;
    } else if (arg == "--emit-obj") {
      opts.emitObj = true;
    } else if (arg.substr(0, 2) == "-O" && arg.length() == 3) {
      opts.optLevel = arg[2] - '0';
      if (opts.optLevel < 0 || opts.optLevel > 3) {
        std::cerr << "Invalid optimization level: " << arg << "\n";
        exit(1);
      }
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      printUsage(argv[0]);
      exit(1);
    }
  }

  return opts;
}

// Read entire file contents into a string
// Exits on file open failure
std::string readFile(const std::string& filename) {
  std::ifstream file(filename);
  if (!file) {
    std::cerr << "Error: Could not open file: " << filename << "\n";
    exit(1);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

// Main entry point for codegen compiler CLI
// Compiles Solis source files to LLVM IR, object files, or executables
int main(int argc, char* argv[]) {
  if (argc < 2 || strcmp(argv[1], "compile") != 0) {
    printUsage(argv[0]);
    return 1;
  }

  CompileOptions opts = parseArgs(argc, argv);

  try {
    // Read source file
    std::cout << "Reading " << opts.inputFile << "...\n";
    std::string source = readFile(opts.inputFile);

    // Lex
    std::cout << "Lexing...\n";
    Lexer lexer(source);
    auto tokens = lexer.tokenize();

    // Parse
    std::cout << "Parsing...\n";
    Parser parser(tokens);
    auto module = parser.parseModule();

    // Type inference
    std::cout << "Type checking...\n";
    TypeInference typer;

    // Type check all declarations
    for (const auto& decl : module.declarations) {
      try {
        typer.inferDecl(*decl);
      } catch (const std::exception& e) {
        std::cerr << "Type error: " << e.what() << "\n";
        return 1;
      }
    }

    // Code generation
    std::cout << "Generating LLVM IR...\n";
    DiagnosticEngine diags;
    CodeGen codegen(opts.inputFile, diags);
    codegen.setTypeInference(&typer);

    codegen.compileModule(module);

    // Check for code generation errors
    if (diags.hasErrors()) {
      std::cerr << "Code generation failed with " << diags.getErrorCount() << " error(s)\n";
      return 1;
    }

    // Verify module
    std::cout << "Verifying module...\n";
    if (!codegen.verifyModule()) {
      return 1;
    }

    // Optimize
    if (opts.optLevel > 0) {
      std::cout << "Optimizing (level " << opts.optLevel << ")...\n";
      OptimizerPipeline::optimize(codegen.getModule(), opts.optLevel);
    }

    // Emit output
    if (opts.emitLLVM) {
      std::cout << "Emitting LLVM IR to " << opts.outputFile << "...\n";
      codegen.emitLLVM(opts.outputFile);
    } else if (opts.emitAsm) {
      std::cout << "Emitting assembly to " << opts.outputFile << "...\n";
      codegen.emitObject(opts.outputFile);
    } else if (opts.emitObj) {
      std::cout << "Emitting object file to " << opts.outputFile << "...\n";
      codegen.emitObject(opts.outputFile);
    } else {
      std::cout << "Emitting executable to " << opts.outputFile << "...\n";
      codegen.emitExecutable(opts.outputFile, opts.optLevel);
    }

    std::cout << "Compilation successful!\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Compilation error: " << e.what() << "\n";
    return 1;
  }
}
