// Solis Programming Language - Target Backend
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "codegen/support/diagnostics.hpp"

#include <llvm/IR/Module.h>

#include <string>

namespace solis {

// Target backend for code emission
// Handles conversion of LLVM IR to machine code and executables
class TargetBackend {
public:
  explicit TargetBackend(DiagnosticEngine& diags)
      : diags_(diags) {}

  // Emit LLVM IR to text file
  // Outputs human-readable LLVM IR for debugging and inspection
  void emitLLVM(llvm::Module* module, const std::string& filename);

  // Emit object file (platform-specific)
  // Generates native object code for linking
  void emitObject(llvm::Module* module, const std::string& filename);

  // Emit executable binary
  // Links object code with runtime library to create standalone executable
  // Performs system-specific linking (clang++ on macOS, g++ on Linux)
  void emitExecutable(llvm::Module* module, const std::string& filename);

private:
  DiagnosticEngine& diags_;

  // Helper to get target triple for current platform
  std::string getTargetTriple(llvm::Module* module);
};

}  // namespace solis
