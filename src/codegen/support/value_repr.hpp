// Solis Programming Language - Value Representation Strategy
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "type/typer.hpp"

#include <cstdint>

namespace solis {

// Professional value representation strategy (as per GHC, Lua, JavaScript engines)
enum class ValueRepr {
  // All values are heap-allocated pointers (like Java, Python)
  Boxed,

  // Small integers unboxed, everything else boxed (hybrid approach)
  // Most balanced for dynamic languages - what GHC does with -O
  Hybrid,

  // Tagged pointers: 63-bit values + 1-bit tag (like Lua 5.3+)
  // Efficient but requires careful pointer manipulation
  TaggedPtr,

  // Full unboxing: int64, double directly (like GHC -O2 with strictness)
  // Best performance but requires complete type information
  Unboxed
};

// Tags for tagged pointer representation
enum class ValueTag : uint8_t {
  Int = 0,   // Integer value (unboxed in pointer)
  Ptr = 1,   // Heap pointer (boxed object)
  Bool = 2,  // Boolean value
  Nil = 3    // Null/unit value
};

// Value representation strategy manager
class ValueRepresentationStrategy {
private:
  ValueRepr strategy_;

public:
  explicit ValueRepresentationStrategy(ValueRepr strategy = ValueRepr::Hybrid)
      : strategy_(strategy) {}

  ValueRepr getStrategy() const { return strategy_; }
  void setStrategy(ValueRepr strategy) { strategy_ = strategy; }

  // Determine if a value should be unboxed based on type
  bool shouldUnbox(const InferTypePtr& type) const {
    if (!type)
      return false;

    switch (strategy_) {
    case ValueRepr::Boxed:
      // Never unbox
      return false;

    case ValueRepr::Unboxed:
      // Always unbox primitives
      return isPrimitive(type);

    case ValueRepr::Hybrid:
      // Unbox small integers and booleans
      return isSmallPrimitive(type);

    case ValueRepr::TaggedPtr:
      // Tag small values in pointer
      return canTagInPointer(type);
    }

    return false;
  }

  // Check if type is a primitive
  bool isPrimitive(const InferTypePtr& type) const {
    if (!type)
      return false;

    if (auto* conTy = std::get_if<InferTyCon>(&type->node)) {
      return conTy->name == "Int" || conTy->name == "Float" || conTy->name == "Bool";
    }
    return false;
  }

  // Check if type is a small primitive (fits in register)
  bool isSmallPrimitive(const InferTypePtr& type) const {
    if (!type)
      return false;

    if (auto* conTy = std::get_if<InferTyCon>(&type->node)) {
      return conTy->name == "Int" || conTy->name == "Bool";
    }
    return false;
  }

  // Check if value can be tagged in pointer
  bool canTagInPointer(const InferTypePtr& type) const {
    if (!type)
      return false;

    if (auto* conTy = std::get_if<InferTyCon>(&type->node)) {
      // Small integers (< 2^63) can be tagged
      return conTy->name == "Int" || conTy->name == "Bool";
    }
    return false;
  }

  // Get value tag for tagged pointer representation
  ValueTag getTag(const InferTypePtr& type) const {
    if (!type)
      return ValueTag::Ptr;

    if (auto* conTy = std::get_if<InferTyCon>(&type->node)) {
      if (conTy->name == "Int")
        return ValueTag::Int;
      if (conTy->name == "Bool")
        return ValueTag::Bool;
    }
    return ValueTag::Ptr;
  }

  // Tag a value for pointer representation (shift left 2 bits, add tag)
  static uint64_t tagValue(int64_t value, ValueTag tag) {
    return (static_cast<uint64_t>(value) << 2) | static_cast<uint64_t>(tag);
  }

  // Untag a value from pointer representation
  static int64_t untagValue(uint64_t tagged) { return static_cast<int64_t>(tagged >> 2); }

  // Get tag from tagged pointer
  static ValueTag getTagFromPointer(uint64_t ptr) { return static_cast<ValueTag>(ptr & 0x3); }

  // Check if pointer is tagged
  static bool isTaggedPointer(uint64_t ptr) {
    return (ptr & 0x3) != static_cast<uint64_t>(ValueTag::Ptr);
  }
};

}  // namespace solis
