// Solis Programming Language - GC Allocation Helpers
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// High-level wrappers for garbage collection operations
// Provides type-safe heap allocation with object tagging
// Handles write barriers for generational GC correctness

#pragma once

#include "codegen/gc/gc_support.hpp"
#include "codegen/runtime/runtime_init.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Value.h>

namespace solis {

// GC Allocation Helper Functions
// Abstracts low-level runtime calls for garbage-collected memory management
class GCHelpers {
public:
  GCHelpers(llvm::IRBuilder<>& builder, llvm::LLVMContext& context, RuntimeFunctions& runtime);

  // Allocate heap object with GC header containing type tag
  // Layout: [GC header | tag byte | object data...]
  // GC header enables object traversal during marking phase
  // Tag identifies object type for specialized handling (ADT, Closure, etc.)
  //
  // Parameters:
  // - size: Total allocation size in bytes (including header)
  // - tag: ObjectTag enum value identifying heap object type
  //
  // Returns: Pointer to allocated memory (points after GC header)
  llvm::Value* createGCAlloc(llvm::Value* size, ObjectTag tag);

  // Allocate atomic (non-pointer) object for leaf values
  // Atomic objects contain no pointers, so GC skips them during marking
  // Used for: strings, numeric arrays, opaque data
  //
  // Performance benefit: Reduces GC scanning overhead
  // Correctness requirement: Must not contain any pointer fields
  llvm::Value* createGCAllocAtomic(llvm::Value* size, ObjectTag tag);

  // Emit write barrier for generational GC
  // Must be called before storing pointer into heap object
  //
  // Write barrier tracks old-to-young generation pointers:
  // - Young objects collected frequently (minor GC)
  // - Old objects collected rarely (major GC)
  // - Barrier records when old object points to young object
  //
  // Parameters:
  // - obj: Container object being modified
  // - field: Address of field being written
  // - value: New pointer value being stored
  //
  // Without barriers, young objects could be prematurely collected
  void emitWriteBarrier(llvm::Value* obj, llvm::Value* field, llvm::Value* value);

private:
  llvm::IRBuilder<>& builder_;
  llvm::LLVMContext& context_;
  RuntimeFunctions& runtime_;

  // Cached type for efficient access
  llvm::Type* int8Type_;
};

}  // namespace solis
