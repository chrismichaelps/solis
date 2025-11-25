// Solis Programming Language - IR Generation Helpers
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/ir/helpers.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>

namespace solis {

IRHelpers::IRHelpers(llvm::LLVMContext& context,
                     llvm::Module& module,
                     llvm::IRBuilder<>& builder,
                     DiagnosticEngine& diags)
    : context_(context)
    , module_(module)
    , builder_(builder)
    , diags_(diags) {
  // Cache frequently used types for performance
  int64Type_ = llvm::Type::getInt64Ty(context_);
  int8PtrType_ = llvm::PointerType::get(context_, 0);
}

llvm::Value* IRHelpers::boxValue(llvm::Value* value, llvm::Type* type) {
  // Value boxing strategy for uniform calling convention
  // Primitives (i64, double) are reinterpreted as pointers via integer conversion
  //
  // Design rationale:
  // - Avoids heap allocation for small values (performance)
  // - Enables passing primitives through generic interfaces
  // - Type information preserved through static analysis
  //
  // Boxing process: value -> ptrtoint -> inttoptr -> i8*
  // This preserves bit pattern while changing type representation
  //
  // Limitation: Only works for values <= pointer size
  // Large values (BigInt) require heap allocation
  if (type->isIntegerTy() || type->isDoubleTy()) {
    // Convert value to integer representation
    llvm::Value* asInt = builder_.CreatePtrToInt(value, int64Type_);
    // Reinterpret integer as pointer
    return builder_.CreateIntToPtr(asInt, int8PtrType_, "boxed");
  }
  // Already a pointer type - no conversion needed
  return value;
}

llvm::Value* IRHelpers::unboxValue(llvm::Value* boxed, llvm::Type* targetType) {
  // Unboxing reverses the boxing process
  // Converts pointer representation back to primitive type
  //
  // Unboxing process: i8* -> ptrtoint -> target_type
  // Must match the original type used for boxing
  //
  // Safety: Caller must ensure boxed value was originally of targetType
  // Type mismatch results in undefined behavior (reinterpretation of bits)
  if (targetType->isIntegerTy() || targetType->isDoubleTy()) {
    return builder_.CreatePtrToInt(boxed, targetType, "unboxed");
  }
  // Already correct pointer type
  return boxed;
}

llvm::Constant* IRHelpers::createStringConstant(const std::string& str) {
  // Global string constant creation for efficient literal storage
  // Design:
  // 1. Create constant data in module's constant section
  // 2. Allocate global variable with private linkage
  // 3. Return pointer to string data
  //
  // Benefits:
  // - Single instance per unique string (deduplication)
  // - Read-only storage in executable's data segment
  // - No runtime allocation overhead
  //
  // LLVM automatically handles string deduplication across module

  // Create null-terminated string constant
  llvm::Constant* strConstant = llvm::ConstantDataArray::getString(context_, str);

  // Allocate global variable in module
  // Private linkage: not visible outside this compilation unit
  // Constant: stored in read-only data segment
  llvm::GlobalVariable* gvar = new llvm::GlobalVariable(module_,
                                                        strConstant->getType(),
                                                        true,
                                                        llvm::GlobalValue::PrivateLinkage,
                                                        strConstant,
                                                        ".str");

  // Get pointer to first character (GEP with two zero indices)
  // First index: offset into global variable (0 = start)
  // Second index: offset into array (0 = first element)
  std::vector<llvm::Value*> indices = {llvm::ConstantInt::get(int64Type_, 0),
                                       llvm::ConstantInt::get(int64Type_, 0)};

  return llvm::ConstantExpr::getGetElementPtr(gvar->getValueType(), gvar, indices);
}

void IRHelpers::emitError(const std::string& message) {
  // Centralized error reporting with "CodeGen:" prefix
  // Enables filtering and categorization of diagnostic messages
  diags_.emitError("CodeGen: " + message);
}

void IRHelpers::emitWarning(const std::string& message) {
  // Warning emission for non-fatal issues
  // Examples: deprecated features, performance concerns, portability issues
  diags_.emitWarning("CodeGen: " + message);
}

}  // namespace solis
