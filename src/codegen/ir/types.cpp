// Solis Programming Language - Type Conversion Module
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/ir/types.hpp"

#include "type/typer.hpp"

#include <llvm/IR/DerivedTypes.h>

namespace solis {

TypeConverter::TypeConverter(llvm::LLVMContext& context)
    : context_(context) {
  // Initialize common types for efficient reuse
  // These types are accessed frequently during IR generation
  voidType_ = llvm::Type::getVoidTy(context_);
  int1Type_ = llvm::Type::getInt1Ty(context_);
  int64Type_ = llvm::Type::getInt64Ty(context_);
  doubleType_ = llvm::Type::getDoubleTy(context_);
  // LLVM 21+ uses opaque pointers - single pointer type for all pointer values
  int8PtrType_ = llvm::PointerType::get(context_, 0);
}

llvm::Type* TypeConverter::toLLVMType(const InferTypePtr& type) {
  if (!type) {
    // Missing type information defaults to boxed representation
    // Allows dynamic dispatch and polymorphism
    return int8PtrType_;
  }

  if (auto* varTy = std::get_if<InferTyVar>(&type->node)) {
    // Type variables represent polymorphic types
    // Must use boxed representation to handle any concrete type at runtime
    return int8PtrType_;
  }

  if (auto* conTy = std::get_if<InferTyCon>(&type->node)) {
    // Primitive types map directly to LLVM scalar types for performance
    // Avoids boxing overhead for common numeric operations
    if (conTy->name == "Int") {
      return int64Type_;
    } else if (conTy->name == "Float") {
      return doubleType_;
    } else if (conTy->name == "Bool") {
      return int1Type_;
    } else if (conTy->name == "String") {
      // Strings as null-terminated C strings (i8*)
      return int8PtrType_;
    } else if (conTy->name == "List") {
      // Lists as cons cell pointers
      // Empty list represented as null pointer
      return int8PtrType_;
    } else {
      // Custom ADTs and records use boxed representation
      // Enables uniform handling of user-defined types
      return int8PtrType_;
    }
  }

  if (auto* funTy = std::get_if<InferTyFun>(&type->node)) {
    // Functions represented as closure pointers
    // Closure captures environment for lexical scoping
    return int8PtrType_;
  }

  // Unknown type variants default to boxed representation
  return int8PtrType_;
}

llvm::Type* TypeConverter::getValueType() {
  // Generic boxed value for polymorphic operations
  // Equivalent to void* in C, but LLVM uses i8* for pointer arithmetic
  return int8PtrType_;
}

llvm::Type* TypeConverter::getThunkType() {
  // Thunk layout for lazy evaluation:
  // Field 0: void* (*compute)(void*) - Computation function
  // Field 1: void* env - Captured environment
  // Field 2: void* cached - Memoized result (null if not evaluated)
  // Field 3: i1 evaluated - Flag indicating if computation has run
  //
  // This design enables call-by-need semantics:
  // - First force() executes compute(env) and caches result
  // - Subsequent force() calls return cached value directly
  std::vector<llvm::Type*> fields = {
      llvm::PointerType::get(llvm::FunctionType::get(int8PtrType_, {int8PtrType_}, false), 0),
      int8PtrType_,
      int8PtrType_,
      int1Type_};
  return llvm::StructType::get(context_, fields);
}

llvm::Type* TypeConverter::getClosureType() {
  // Closure layout for first-class functions:
  // Field 0: void* fn_ptr - Function pointer (uniform signature: void* fn(void*, void*))
  // Field 1: void* env - Captured environment (array of free variables)
  //
  // All functions use uniform calling convention:
  // - First parameter: environment pointer (captures)
  // - Second parameter: function argument
  // - Return: function result (boxed if needed)
  //
  // Curried functions return closures capturing partial arguments
  std::vector<llvm::Type*> fields = {int8PtrType_, int8PtrType_};
  return llvm::StructType::get(context_, fields);
}

llvm::Type* TypeConverter::getConsType() {
  // Cons cell layout for linked lists:
  // Field 0: void* head - List element (boxed value)
  // Field 1: void* tail - Pointer to next cons cell (null for empty list)
  //
  // List construction: cons(elem, list) allocates new cell
  // List deconstruction: head(list) and tail(list) extract fields
  // Empty list: null pointer (no allocation needed)
  //
  // This design enables efficient structural sharing:
  // Multiple lists can share tails without copying
  std::vector<llvm::Type*> fields = {int8PtrType_, int8PtrType_};
  return llvm::StructType::get(context_, fields);
}

}  // namespace solis
