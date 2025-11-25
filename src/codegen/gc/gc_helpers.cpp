// Solis Programming Language - GC Allocation Helpers
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/gc/gc_helpers.hpp"

#include <llvm/IR/Constants.h>

namespace solis {

GCHelpers::GCHelpers(llvm::IRBuilder<>& builder,
                     llvm::LLVMContext& context,
                     RuntimeFunctions& runtime)
    : builder_(builder)
    , context_(context)
    , runtime_(runtime) {
  int8Type_ = llvm::Type::getInt8Ty(context_);
}

llvm::Value* GCHelpers::createGCAlloc(llvm::Value* size, ObjectTag tag) {
  // Heap allocation via runtime allocator
  // Runtime function: void* solis_alloc(size_t size, uint8_t tag)
  //
  // Design rationale:
  // - Type tag enables object-specific GC behavior
  // - Header space prepended by runtime allocator
  // - Returns pointer to usable object data (after header)
  //
  // Object layout in memory:
  // [GC header (mark bits, size, etc.) | tag byte | user data...]
  //
  // Tag usage:
  // - ADT: Check constructor tag for pattern matching
  // - Closure: Trace environment pointer during GC
  // - String: Skip tracing (atomic object)
  //
  // The GC uses tags to determine:
  // 1. How to traverse object during marking
  // 2. Whether object is atomic (no pointers)
  // 3. Type-specific finalization requirements

  llvm::Value* tagVal = llvm::ConstantInt::get(int8Type_, static_cast<uint8_t>(tag));

  return builder_.CreateCall(runtime_.allocFunc, {size, tagVal}, "alloc");
}

llvm::Value* GCHelpers::createGCAllocAtomic(llvm::Value* size, ObjectTag tag) {
  // Atomic allocation for pointer-free objects
  // Runtime function: void* solis_alloc_atomic(size_t size, uint8_t tag)
  //
  // "Atomic" means object contains no pointers to other heap objects
  // GC optimization: Skip scanning atomic objects during marking phase
  //
  // Use cases:
  // - String data (char array)
  // - Numeric buffers (int[], double[])
  // - Opaque FFI data
  //
  // Performance impact:
  // - Reduces GC marking time proportional to atomic object size
  // - Critical for large string processing workloads
  //
  // Correctness requirement:
  // Storing pointer into atomic object breaks GC invariant
  // Result: Stored pointer becomes invisible to GC, causing premature collection

  llvm::Value* tagVal = llvm::ConstantInt::get(int8Type_, static_cast<uint8_t>(tag));

  return builder_.CreateCall(runtime_.allocAtomicFunc, {size, tagVal}, "alloc_atomic");
}

void GCHelpers::emitWriteBarrier(llvm::Value* obj, llvm::Value* field, llvm::Value* value) {
  // Write barrier for generational garbage collection
  // Runtime function: void solis_gc_write_barrier(void* obj, void* field, void* value)
  //
  // Generational GC design:
  // - Heap divided into young generation (nursery) and old generation
  // - Minor GC: Collect only young generation (frequent, fast)
  // - Major GC: Collect entire heap (rare, slow)
  //
  // Problem: Old-to-young pointers
  // Without tracking, minor GC could collect young objects still referenced by old objects
  //
  // Solution: Write barrier
  // 1. Before storing pointer: obj->field = value
  // 2. Check if obj is old and value is young
  // 3. If yes, record obj in "remembered set"
  // 4. Minor GC treats remembered set as additional roots
  //
  // Cost: Small overhead per pointer store
  // Benefit: Enables efficient minor GC (collects only young generation)
  //
  // Implementation note:
  // Compiler emits barrier call before pointer stores
  // Runtime checks generation and updates remembered set if needed

  builder_.CreateCall(runtime_.gcWriteBarrierFunc, {obj, field, value});
}

}  // namespace solis
