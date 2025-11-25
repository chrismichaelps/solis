// Solis Programming Language - GC Support Infrastructure
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include <cstdint>

namespace solis {

// Object tags for GC tracing and type identification
// These tags enable the GC to know which fields contain pointers
enum class ObjectTag : uint8_t {
  // Primitive types (no pointers to trace)
  Int = 0,
  Float = 1,
  Bool = 2,

  // Pointer-containing types (GC must trace)
  String = 3,
  Closure = 4,
  Thunk = 5,
  ConsList = 6,
  ADT = 7,  // Algebraic data type
  Record = 8,
  Environment = 9,  // Closure environment

  // Special markers
  Forwarding = 255  // For copying GC (Cheney algorithm)
};

// Object header layout for GC-managed objects
// Placed at the beginning of every heap allocation
struct ObjectHeader {
  uint64_t metadata;  // Packed: [tag:8][gc_mark:1][reserved:55]

  // Extract tag from header
  static ObjectTag getTag(uint64_t header) { return static_cast<ObjectTag>(header & 0xFF); }

  // Create header with tag
  static uint64_t makeHeader(ObjectTag tag) { return static_cast<uint64_t>(tag); }

  // Check if object contains pointers (needs tracing)
  static bool containsPointers(ObjectTag tag) {
    return tag >= ObjectTag::String && tag <= ObjectTag::Environment;
  }
};

// GC write barrier modes
enum class WriteBarrierMode {
  None,          // No barrier (for initialization)
  Generational,  // For generational GC (young->old references)
  Incremental    // For incremental/concurrent GC
};

}  // namespace solis
