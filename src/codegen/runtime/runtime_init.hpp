// Solis Programming Language - Runtime Function Initialization
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "codegen/gc/gc_support.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace solis {

// Runtime function registry
// Manages declaration and initialization of all runtime functions
// used by the code generator for built-in operations
class RuntimeFunctions {
public:
  // Runtime function handles using FunctionCallee for type safety
  llvm::FunctionCallee allocFunc;           // GC allocation with tag
  llvm::FunctionCallee allocAtomicFunc;     // GC allocation for non-pointer data
  llvm::FunctionCallee gcWriteBarrierFunc;  // GC write barrier for pointer stores
  llvm::FunctionCallee createThunkFunc;     // Create lazy thunk
  llvm::FunctionCallee forceThunkFunc;      // Force thunk evaluation
  llvm::FunctionCallee stringConcatFunc;    // String concatenation
  llvm::FunctionCallee stringEqFunc;        // String equality
  llvm::FunctionCallee printFunc;           // Print string
  llvm::FunctionCallee readLineFunc;        // Read line from stdin
  llvm::FunctionCallee consFunc;            // Create cons cell
  llvm::FunctionCallee headFunc;            // List head
  llvm::FunctionCallee tailFunc;            // List tail
  llvm::FunctionCallee lengthFunc;          // List length

  // Initialize all runtime functions in the given module
  // Sets up function declarations with proper types and attributes
  void initialize(llvm::Module* module, llvm::LLVMContext& context);

private:
  // Helper to add function attributes for optimization
  void addOptimizationAttributes(llvm::Function* func,
                                 bool noUnwind = true,
                                 bool readNone = false,
                                 bool readOnly = false);
};

}  // namespace solis
