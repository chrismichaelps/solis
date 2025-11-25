// Solis Programming Language - Type Conversion Module
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Type conversion between Solis type system and LLVM IR types
// Handles mapping of language-level types to low-level LLVM representations
// Provides structural types for runtime constructs (thunks, closures, cons cells)

#pragma once

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Type.h>

#include <memory>

namespace solis {

// Forward declarations from type inference
struct InferType;
using InferTypePtr = std::shared_ptr<InferType>;

// Type Conversion Module
// Converts Solis inference types to LLVM IR types
// Manages structural types for runtime data structures
class TypeConverter {
public:
  TypeConverter(llvm::LLVMContext& context);

  // Primary type conversion
  // Maps Solis types (Int, Float, Bool, String, List, functions, ADTs) to LLVM types
  // Type variables and unknown types default to boxed representation (i8*)
  llvm::Type* toLLVMType(const InferTypePtr& type);

  // Generic boxed value type (void* in C, i8* in LLVM)
  // Used for polymorphic values and type variables
  llvm::Type* getValueType();

  // Thunk structure: { fn_ptr, env_ptr, cached_result, is_evaluated }
  // Enables lazy evaluation by deferring computation until forced
  llvm::Type* getThunkType();

  // Closure structure: { fn_ptr, env_ptr }
  // Captures free variables for first-class function support
  llvm::Type* getClosureType();

  // Cons cell structure: { head, tail }
  // Represents linked list nodes for List type
  llvm::Type* getConsType();

  // Common LLVM types for convenient access
  llvm::Type* getVoidType() const { return voidType_; }
  llvm::Type* getInt1Type() const { return int1Type_; }
  llvm::Type* getInt64Type() const { return int64Type_; }
  llvm::Type* getDoubleType() const { return doubleType_; }
  llvm::Type* getInt8PtrType() const { return int8PtrType_; }

private:
  llvm::LLVMContext& context_;

  // Cached common types to avoid repeated construction
  llvm::Type* voidType_;
  llvm::Type* int1Type_;
  llvm::Type* int64Type_;
  llvm::Type* doubleType_;
  llvm::Type* int8PtrType_;
};

}  // namespace solis
