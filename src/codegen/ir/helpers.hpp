// Solis Programming Language - IR Generation Helpers
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Utility functions for IR generation
// Handles value boxing/unboxing for uniform representation
// Manages string constant creation and diagnostic wrappers

#pragma once

#include "codegen/support/diagnostics.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <string>

namespace solis {

// IR Generation Helper Functions
// Provides utilities for common IR construction patterns
class IRHelpers {
public:
  IRHelpers(llvm::LLVMContext& context,
            llvm::Module& module,
            llvm::IRBuilder<>& builder,
            DiagnosticEngine& diags);

  // Value boxing and unboxing for uniform representation
  // Boxing converts unboxed primitives (i64, double) to pointer representation
  // Enables passing primitives through generic function interfaces
  // Required when function parameters expect boxed values (void*)
  llvm::Value* boxValue(llvm::Value* value, llvm::Type* type);

  // Unboxing converts pointer representation back to primitive types
  // Required when performing arithmetic or comparison on boxed values
  // Inverse operation of boxValue - extract underlying primitive
  llvm::Value* unboxValue(llvm::Value* boxed, llvm::Type* targetType);

  // Global string constant creation
  // Creates null-terminated C-style string in module's constant section
  // Returns pointer to string data (i8*)
  // Multiple references to same string share single global constant
  llvm::Constant* createStringConstant(const std::string& str);

  // Diagnostic wrappers for error and warning emission
  // Provides centralized error reporting with context information
  void emitError(const std::string& message);
  void emitWarning(const std::string& message);

private:
  llvm::LLVMContext& context_;
  llvm::Module& module_;
  llvm::IRBuilder<>& builder_;
  DiagnosticEngine& diags_;

  // Cached common types for efficient access
  llvm::Type* int64Type_;
  llvm::Type* int8PtrType_;
};

}  // namespace solis
