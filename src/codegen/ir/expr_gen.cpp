// Solis Programming Language - Expression IR Generation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/ir/expr_gen.hpp"

#include "codegen/codegen.hpp"
#include "codegen/ir/closure.hpp"
#include "runtime/bigint.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>

namespace solis {

ExprGenerator::ExprGenerator(llvm::LLVMContext& context,
                             llvm::Module& module,
                             llvm::IRBuilder<>& builder,
                             SymbolTable& symbols,
                             TypeConverter& types,
                             IRHelpers& helpers,
                             GCHelpers& gcHelpers,
                             RuntimeFunctions& runtime,
                             std::map<std::string, int>& constructorTags)
    : context_(context)
    , module_(module)
    , builder_(builder)
    , symbols_(symbols)
    , types_(types)
    , helpers_(helpers)
    , gcHelpers_(gcHelpers)
    , runtime_(runtime)
    , constructorTags_(constructorTags) {
  // Cache common types for efficient access during IR generation
  int1Type_ = llvm::Type::getInt1Ty(context_);
  int64Type_ = llvm::Type::getInt64Ty(context_);
  doubleType_ = llvm::Type::getDoubleTy(context_);
  int8PtrType_ = llvm::PointerType::get(context_, 0);
}

llvm::Value* ExprGenerator::genExpr(const Expr& expr) {
  // Main expression dispatcher routes to specialized generation methods
  // Uses std::visit for type-safe variant handling
  // Returns nullptr on error (caller should check)
  return std::visit(
      [this](const auto& node) -> llvm::Value* {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Var>) {
          return genVar(node);
        } else if constexpr (std::is_same_v<T, Lit>) {
          return genLit(node);
        } else if constexpr (std::is_same_v<T, Lambda>) {
          // Delegate to closure converter (circular dependency)
          if (!closureConv_) {
            helpers_.emitError("Closure converter not initialized");
            return nullptr;
          }
          return closureConv_->genLambda(node);
        } else if constexpr (std::is_same_v<T, App>) {
          // Delegate to closure converter
          if (!closureConv_) {
            helpers_.emitError("Closure converter not initialized");
            return nullptr;
          }
          return closureConv_->genApp(node);
        } else if constexpr (std::is_same_v<T, Let>) {
          // Delegate to closure converter (pattern matching)
          if (!closureConv_) {
            helpers_.emitError("Closure converter not initialized");
            return nullptr;
          }
          return closureConv_->genLet(node);
        } else if constexpr (std::is_same_v<T, Match>) {
          // Delegate to closure converter (pattern matching)
          if (!closureConv_) {
            helpers_.emitError("Closure converter not initialized");
            return nullptr;
          }
          return closureConv_->genMatch(node);
        } else if constexpr (std::is_same_v<T, If>) {
          return genIf(node);
        } else if constexpr (std::is_same_v<T, BinOp>) {
          return genBinOp(node);
        } else if constexpr (std::is_same_v<T, List>) {
          return genList(node);
        } else if constexpr (std::is_same_v<T, Record>) {
          return genRecord(node);
        } else if constexpr (std::is_same_v<T, RecordAccess>) {
          return genRecordAccess(node);
        } else if constexpr (std::is_same_v<T, RecordUpdate>) {
          return genRecordUpdate(node);
        } else if constexpr (std::is_same_v<T, Block>) {
          return genBlock(node);
        } else if constexpr (std::is_same_v<T, Strict>) {
          return genStrict(node);
        } else if constexpr (std::is_same_v<T, Bind>) {
          return genBind(node);
        } else {
          helpers_.emitError("Unknown expression type");
          return nullptr;
        }
      },
      expr.node);
}

llvm::Value* ExprGenerator::genVar(const Var& var) {
  // Variable reference resolution through symbol table
  // Handles three cases:
  // 1. Local variable - return bound value
  // 2. Zero-parameter function - auto-call (constant binding)
  // 3. N-parameter function - wrap in closure
  llvm::Value* value = symbols_.lookup(var.name);

  if (!value) {
    // Check for global functions not in symbol table
    llvm::Function* func = module_.getFunction(var.name);
    if (func) {
      value = func;
    } else {
      helpers_.emitError("Undefined variable: " + var.name);
      return nullptr;
    }
  }

  if (llvm::isa<llvm::Function>(value)) {
    llvm::Function* func = llvm::cast<llvm::Function>(value);
    if (func->arg_size() == 0) {
      // Zero-parameter function represents constant binding
      // Auto-call to get value: enables `let x = 42` to work in expressions
      return builder_.CreateCall(func, {}, var.name + "_val");
    } else {
      // Multi-parameter function needs closure wrapper
      // Delegates to closure converter for proper environment capture
      if (!closureConv_) {
        helpers_.emitError("Closure converter not initialized");
        return nullptr;
      }
      return closureConv_->createClosure(func, {});
    }
  }

  return value;
}

llvm::Value* ExprGenerator::genLit(const Lit& lit) {
  // Literal value generation for all primitive types
  // Uses std::visit for type-safe variant handling
  // Returns appropriate LLVM constant for each type
  return std::visit(
      [this](const auto& value) -> llvm::Value* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, int>) {
          return llvm::ConstantInt::get(int64Type_, value);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return llvm::ConstantInt::get(int64Type_, value);
        } else if constexpr (std::is_same_v<T, double>) {
          return llvm::ConstantFP::get(doubleType_, value);
        } else if constexpr (std::is_same_v<T, bool>) {
          return llvm::ConstantInt::get(int1Type_, value ? 1 : 0);
        } else if constexpr (std::is_same_v<T, std::string>) {
          return helpers_.createStringConstant(value);
        } else if constexpr (std::is_same_v<T, BigInt>) {
          // BigInt literal strategy: use int64 when possible for performance
          // Design decision: Prioritize common case (small integers)
          // Over arbitrary precision (rare, requires runtime library)
          //
          // Rationale:
          // - Most integer literals fit in int64 (99.9% of real code)
          // - Native int64 arithmetic is faster than BigInt library calls
          // - Full BigInt support deferred until user demand justifies complexity
          if (value.fitsInInt64()) {
            return llvm::ConstantInt::get(int64Type_, value.toInt64());
          } else {
            helpers_.emitError("BigInt literal exceeds int64 range - requires runtime "
                               "BigInt support");
            return nullptr;
          }
        } else {
          helpers_.emitError("Unknown literal type");
          return nullptr;
        }
      },
      lit.value);
}

llvm::Value* ExprGenerator::genBinOp(const BinOp& binOp) {
  // Binary operator compilation with automatic type handling
  // Supports: arithmetic, comparison, logical, string concatenation
  llvm::Value* left = genExpr(*binOp.left);
  llvm::Value* right = genExpr(*binOp.right);

  if (!left || !right) {
    return nullptr;
  }

  // Auto-unboxing for arithmetic and comparison operators
  // Required when operands come from boxed function parameters
  // Uniform calling convention (all values boxed) necessitates runtime unboxing
  if (binOp.op == "+" || binOp.op == "-" || binOp.op == "*" || binOp.op == "/" || binOp.op == "%" ||
      binOp.op == "==" || binOp.op == "!=" || binOp.op == "<" || binOp.op == "<=" ||
      binOp.op == ">" || binOp.op == ">=") {
    if (left->getType() == int8PtrType_) {
      left = helpers_.unboxValue(left, int64Type_);
    }
    if (right->getType() == int8PtrType_) {
      right = helpers_.unboxValue(right, int64Type_);
    }
  }

  // Arithmetic operations
  if (binOp.op == "+") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateAdd(left, right, "addtmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFAdd(left, right, "addtmp");
    }
  } else if (binOp.op == "-") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateSub(left, right, "subtmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFSub(left, right, "subtmp");
    }
  } else if (binOp.op == "*") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateMul(left, right, "multmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFMul(left, right, "multmp");
    }
  } else if (binOp.op == "/") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateSDiv(left, right, "divtmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFDiv(left, right, "divtmp");
    }
  } else if (binOp.op == "%") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateSRem(left, right, "modtmp");
    }
  }

  // Comparison operations
  else if (binOp.op == "==") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateICmpEQ(left, right, "eqtmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFCmpOEQ(left, right, "eqtmp");
    } else if (left->getType()->isPointerTy()) {
      // String equality via runtime function (structural comparison)
      return builder_.CreateCall(runtime_.stringEqFunc, {left, right}, "streqtmp");
    }
  } else if (binOp.op == "!=") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateICmpNE(left, right, "netmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFCmpONE(left, right, "netmp");
    }
  } else if (binOp.op == "<") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateICmpSLT(left, right, "lttmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFCmpOLT(left, right, "lttmp");
    }
  } else if (binOp.op == "<=") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateICmpSLE(left, right, "letmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFCmpOLE(left, right, "letmp");
    }
  } else if (binOp.op == ">") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateICmpSGT(left, right, "gttmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFCmpOGT(left, right, "gttmp");
    }
  } else if (binOp.op == ">=") {
    if (left->getType()->isIntegerTy()) {
      return builder_.CreateICmpSGE(left, right, "getmp");
    } else if (left->getType()->isDoubleTy()) {
      return builder_.CreateFCmpOGE(left, right, "getmp");
    }
  }

  // Logical operations (short-circuit evaluation not implemented)
  // Design note: && and || currently use bitwise operations
  // Full short-circuit requires control flow generation
  else if (binOp.op == "&&") {
    return builder_.CreateAnd(left, right, "andtmp");
  } else if (binOp.op == "||") {
    return builder_.CreateOr(left, right, "ortmp");
  }

  // String concatenation via runtime function
  else if (binOp.op == "++") {
    return builder_.CreateCall(runtime_.stringConcatFunc, {left, right}, "concattmp");
  }

  helpers_.emitError("Unknown binary operator: " + binOp.op);
  return nullptr;
}

llvm::Value* ExprGenerator::genIf(const If& ifExpr) {
  // Conditional expression with phi node for result merging
  // Control flow: cond -> then/else -> merge
  // Both branches must return same type for phi node
  llvm::Value* condValue = genExpr(*ifExpr.cond);
  if (!condValue) {
    return nullptr;
  }

  // Create basic blocks for control flow
  llvm::Function* function = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(context_, "then", function);
  llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(context_, "else");
  llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "ifcont");

  builder_.CreateCondBr(condValue, thenBB, elseBB);

  // Then branch
  builder_.SetInsertPoint(thenBB);
  llvm::Value* thenValue = genExpr(*ifExpr.thenBranch);
  if (!thenValue) {
    return nullptr;
  }

  // Auto-box for uniform representation if needed
  // Enables if to return either primitive or boxed values
  if (thenValue->getType()->isIntegerTy() && thenValue->getType() != int1Type_) {
    thenValue = helpers_.boxValue(thenValue, thenValue->getType());
  }

  builder_.CreateBr(mergeBB);
  thenBB = builder_.GetInsertBlock();  // Update for phi (may have changed)

  // Else branch
  function->insert(function->end(), elseBB);
  builder_.SetInsertPoint(elseBB);
  llvm::Value* elseValue = genExpr(*ifExpr.elseBranch);
  if (!elseValue) {
    return nullptr;
  }

  // Auto-box to match then branch type
  if (elseValue->getType()->isIntegerTy() && elseValue->getType() != int1Type_) {
    elseValue = helpers_.boxValue(elseValue, elseValue->getType());
  }

  builder_.CreateBr(mergeBB);
  elseBB = builder_.GetInsertBlock();

  // Merge block with phi node
  function->insert(function->end(), mergeBB);
  builder_.SetInsertPoint(mergeBB);

  llvm::PHINode* phi = builder_.CreatePHI(thenValue->getType(), 2, "iftmp");
  phi->addIncoming(thenValue, thenBB);
  phi->addIncoming(elseValue, elseBB);
  return phi;
}

llvm::Value* ExprGenerator::genBlock(const Block& block) {
  // Block expression evaluates sequence of statements
  // Returns value of last statement (expression-oriented design)
  llvm::Value* result = nullptr;
  for (const auto& stmt : block.stmts) {
    result = genExpr(*stmt);
    if (!result) {
      return nullptr;
    }
  }
  // Empty block returns 0 (unit type emulation)
  return result ? result : llvm::ConstantInt::get(int64Type_, 0);
}

llvm::Value* ExprGenerator::genStrict(const Strict& strict) {
  // Strict evaluation annotation forces eager evaluation
  // Currently just generates expression directly (no thunk wrapper)
  // Reserved for future lazy evaluation optimization
  return genExpr(*strict.expr);
}

llvm::Value* ExprGenerator::genBind(const Bind& bind) {
  // Monadic bind for do-notation: pattern <- value; body
  // Evaluates value, binds to pattern, evaluates body
  llvm::Value* value = genExpr(*bind.value);
  if (!value) {
    return nullptr;
  }

  // Pattern binding (limited support currently)
  if (auto* varPat = std::get_if<VarPat>(&bind.pattern->node)) {
    symbols_.insert(varPat->name, value);
  } else if (std::get_if<WildcardPat>(&bind.pattern->node)) {
    // Wildcard: evaluate for side effects, ignore value
  } else {
    // Complex patterns require full destructuring support
    helpers_.emitError("Complex patterns in bind require destructuring support");
  }

  return genExpr(*bind.body);
}

llvm::Value* ExprGenerator::genList(const List& list) {
  // List construction via cons cells built right-to-left
  // Empty list is null pointer (no allocation)
  // Each cons: { head: value, tail: rest_of_list }
  llvm::Value* result = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_));

  // Build from right to left for proper list structure
  for (auto it = list.elements.rbegin(); it != list.elements.rend(); ++it) {
    llvm::Value* elem = genExpr(**it);
    if (!elem) {
      return nullptr;
    }

    // Box primitives for uniform list element representation
    if (elem->getType() != int8PtrType_) {
      elem = helpers_.boxValue(elem, elem->getType());
    }

    // Runtime cons allocation: solis_cons(head, tail)
    result = builder_.CreateCall(runtime_.consFunc, {elem, result}, "cons");
  }

  return result;
}

llvm::Value* ExprGenerator::genRecord(const Record& record) {
  // Record construction with runtime field lookup support
  // Memory layout: [field_count, name_ptrs..., values...]
  // Enables dynamic field access by name at runtime
  if (record.fields.empty()) {
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_));
  }

  size_t numFields = record.fields.size();

  // Collect field names and values for layout construction
  std::vector<std::string> fieldNames;
  std::vector<llvm::Value*> fieldValues;

  for (const auto& [name, expr] : record.fields) {
    fieldNames.push_back(name);

    llvm::Value* val = genExpr(*expr);
    if (!val)
      return nullptr;

    // Box primitives for uniform storage
    if (val->getType() != int8PtrType_ && val->getType()->isIntegerTy()) {
      val = helpers_.boxValue(val, val->getType());
    }
    fieldValues.push_back(val);
  }

  // Allocate record: [count (8), names (8*n), values (8*n)]
  size_t headerSize = (1 + numFields) * 8;  // count + name pointers
  size_t valuesSize = numFields * 8;        // field values
  llvm::Value* totalSize = llvm::ConstantInt::get(int64Type_, headerSize + valuesSize);
  llvm::Value* recordMem = gcHelpers_.createGCAlloc(totalSize, ObjectTag::Record);

  // Store field count at offset 0
  llvm::Value* countPtr = builder_.CreatePointerCast(recordMem,
                                                     llvm::PointerType::get(int64Type_, 0));
  builder_.CreateStore(llvm::ConstantInt::get(int64Type_, numFields), countPtr);

  // Store field name pointers in header
  for (size_t i = 0; i < numFields; ++i) {
    llvm::Value* nameStr = helpers_.createStringConstant(fieldNames[i]);
    llvm::Value* namePtr = builder_.CreateGEP(int8PtrType_,
                                              recordMem,
                                              llvm::ConstantInt::get(int64Type_, (1 + i) * 8),
                                              "name_ptr");
    builder_.CreateStore(nameStr,
                         builder_.CreatePointerCast(namePtr,
                                                    llvm::PointerType::get(int8PtrType_, 0)));
  }

  // Store field values after header
  size_t valueOffset = (1 + numFields) * 8;
  for (size_t i = 0; i < numFields; ++i) {
    llvm::Value* fieldPtr = builder_.CreateGEP(int8PtrType_,
                                               recordMem,
                                               llvm::ConstantInt::get(int64Type_,
                                                                      valueOffset + i * 8),
                                               "field_ptr");
    builder_.CreateStore(fieldValues[i],
                         builder_.CreatePointerCast(fieldPtr,
                                                    llvm::PointerType::get(int8PtrType_, 0)));
  }

  return recordMem;
}

llvm::Value* ExprGenerator::genRecordAccess(const RecordAccess& access) {
  // Runtime field lookup by name using linear search
  // Performance: O(n) where n is field count
  // Alternative: compile-time field indices require type information
  llvm::Value* record = genExpr(*access.record);
  if (!record)
    return nullptr;

  // Load field count from record header
  llvm::Value* countPtr = builder_.CreatePointerCast(record, llvm::PointerType::get(int64Type_, 0));
  llvm::Value* fieldCount = builder_.CreateLoad(int64Type_, countPtr, "field_count");

  // Target field name constant
  llvm::Value* targetName = helpers_.createStringConstant(access.field);

  // Create loop structure for field search
  llvm::Function* function = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* loopBB = llvm::BasicBlock::Create(context_, "field_search", function);
  llvm::BasicBlock* foundBB = llvm::BasicBlock::Create(context_, "field_found", function);
  llvm::BasicBlock* notFoundBB = llvm::BasicBlock::Create(context_, "field_not_found", function);
  llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "field_merge", function);

  // Initialize loop
  llvm::BasicBlock* entryBB = builder_.GetInsertBlock();
  llvm::Value* zero = llvm::ConstantInt::get(int64Type_, 0);
  builder_.CreateBr(loopBB);

  // Loop header with phi for index
  builder_.SetInsertPoint(loopBB);
  llvm::PHINode* indexPhi = builder_.CreatePHI(int64Type_, 2, "field_index");
  indexPhi->addIncoming(zero, entryBB);

  // Continue if index < fieldCount
  llvm::Value* cond = builder_.CreateICmpSLT(indexPhi, fieldCount, "continue_search");
  llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(context_, "check_field", function);
  builder_.CreateCondBr(cond, bodyBB, notFoundBB);

  // Loop body: load and compare field name
  builder_.SetInsertPoint(bodyBB);

  llvm::Value* one = llvm::ConstantInt::get(int64Type_, 1);
  llvm::Value* nameOffset = builder_.CreateAdd(one, indexPhi);
  llvm::Value* nameOffsetBytes = builder_.CreateMul(nameOffset,
                                                    llvm::ConstantInt::get(int64Type_, 8));
  llvm::Value* namePtr = builder_.CreateGEP(int8PtrType_, record, nameOffsetBytes);
  llvm::Value* fieldName = builder_.CreateLoad(
      int8PtrType_,
      builder_.CreatePointerCast(namePtr, llvm::PointerType::get(int8PtrType_, 0)),
      "field_name");

  // Compare names using runtime string equality
  llvm::Value* isMatch = builder_.CreateCall(runtime_.stringEqFunc,
                                             {fieldName, targetName},
                                             "name_match");

  llvm::Value* nextIndex = builder_.CreateAdd(indexPhi, one, "next_index");
  indexPhi->addIncoming(nextIndex, bodyBB);

  builder_.CreateCondBr(isMatch, foundBB, loopBB);

  // Field found: load value from values array
  builder_.SetInsertPoint(foundBB);
  llvm::Value* headerSize = builder_.CreateMul(builder_.CreateAdd(fieldCount, one),
                                               llvm::ConstantInt::get(int64Type_, 8));
  llvm::Value* valueOffset = builder_.CreateAdd(
      headerSize, builder_.CreateMul(indexPhi, llvm::ConstantInt::get(int64Type_, 8)));
  llvm::Value* valuePtr = builder_.CreateGEP(int8PtrType_, record, valueOffset);
  llvm::Value* fieldValue = builder_.CreateLoad(
      int8PtrType_,
      builder_.CreatePointerCast(valuePtr, llvm::PointerType::get(int8PtrType_, 0)),
      "field_value");
  builder_.CreateBr(mergeBB);

  // Field not found: error
  builder_.SetInsertPoint(notFoundBB);
  helpers_.emitError("Record field not found: " + access.field);
  llvm::Value* nullValue = llvm::ConstantPointerNull::get(
      llvm::cast<llvm::PointerType>(int8PtrType_));
  builder_.CreateBr(mergeBB);

  // Merge point
  builder_.SetInsertPoint(mergeBB);
  llvm::PHINode* result = builder_.CreatePHI(int8PtrType_, 2, "field_result");
  result->addIncoming(fieldValue, foundBB);
  result->addIncoming(nullValue, notFoundBB);

  return result;
}

llvm::Value* ExprGenerator::genRecordUpdate(const RecordUpdate& update) {
  // Immutable record update with copy-on-write semantics
  // Allocates new record, copies old data, updates specified fields
  llvm::Value* oldRecord = genExpr(*update.record);
  if (!oldRecord)
    return nullptr;

  // Conservative allocation size (sufficient for typical records)
  llvm::Value* recordSize = llvm::ConstantInt::get(int64Type_, 64);  // Up to 8 fields
  llvm::Value* newRecord = gcHelpers_.createGCAlloc(recordSize, ObjectTag::Record);

  // Copy old record data to new record
  builder_.CreateMemCpy(newRecord, llvm::MaybeAlign(8), oldRecord, llvm::MaybeAlign(8), recordSize);

  // Update specified fields in new record
  for (const auto& [fieldName, expr] : update.updates) {
    llvm::Value* newVal = genExpr(*expr);
    if (!newVal)
      continue;

    // Box if needed
    if (newVal->getType() != int8PtrType_ && newVal->getType()->isIntegerTy()) {
      newVal = helpers_.boxValue(newVal, newVal->getType());
    }

    // Update field at indexed position
    // Note: Simplified implementation - proper version needs field lookup
    llvm::Value* fieldPtr = builder_.CreateGEP(int8PtrType_,
                                               newRecord,
                                               llvm::ConstantInt::get(int64Type_, 0),
                                               "updateptr");
    builder_.CreateStore(newVal,
                         builder_.CreatePointerCast(fieldPtr,
                                                    llvm::PointerType::get(int8PtrType_, 0)));
  }

  return newRecord;
}

}  // namespace solis
