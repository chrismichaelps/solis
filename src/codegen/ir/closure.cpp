// Solis Programming Language - Closure Conversion Module
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/ir/closure.hpp"

#include "codegen/codegen.hpp"
#include "codegen/ir/expr_gen.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>

namespace solis {

// Helper function to collect bound variables from pattern
// Used for free variable analysis during closure conversion
static void collectPatternVars(const Pattern& pat, std::set<std::string>& vars) {
  std::visit(
      [&](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, VarPat>) {
          vars.insert(node.name);
        } else if constexpr (std::is_same_v<T, ConsPat>) {
          for (const auto& arg : node.args)
            collectPatternVars(*arg, vars);
        } else if constexpr (std::is_same_v<T, ListPat>) {
          for (const auto& elem : node.elements)
            collectPatternVars(*elem, vars);
        } else if constexpr (std::is_same_v<T, RecordPat>) {
          for (const auto& [name, p] : node.fields)
            collectPatternVars(*p, vars);
        }
      },
      pat.node);
}

ClosureConverter::ClosureConverter(llvm::LLVMContext& context,
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
  // Cache common types
  int1Type_ = llvm::Type::getInt1Ty(context_);
  int64Type_ = llvm::Type::getInt64Ty(context_);
  int8PtrType_ = llvm::PointerType::get(context_, 0);
}

llvm::Value* ClosureConverter::genLambda(const Lambda& lambda, const std::string& recursiveName) {
  // Lambda compilation strategy:
  // 1. Analyze free variables for capture
  // 2. Generate curried functions for multi-parameter lambdas
  // 3. Create closure structure with environment
  //
  // recursiveName reserved for future let rec syntax support
  // Currently unused but parameter maintained for API compatibility
  (void)recursiveName;

  return genLambdaImpl(lambda.params, lambda.body);
}

llvm::Value* ClosureConverter::genApp(const App& app) {
  // Function application unpacks closure and calls with environment
  // Closure layout: { fn_ptr: void*(*)(void*, void*), env_ptr: void* }
  //
  // Call sequence:
  // 1. Load function pointer from closure (offset 0)
  // 2. Load environment pointer from closure (offset 1)
  // 3. Box argument if needed for uniform calling convention
  // 4. Call: result = fn_ptr(env_ptr, arg)

  if (!exprGen_) {
    helpers_.emitError("Expression generator not initialized");
    return nullptr;
  }

  llvm::Value* func = exprGen_->genExpr(*app.func);
  llvm::Value* arg = exprGen_->genExpr(*app.arg);

  if (!func || !arg) {
    return nullptr;
  }

  // Box argument for uniform calling convention
  // All function parameters are void* to enable polymorphism
  if (arg->getType()->isIntegerTy() && arg->getType() != int1Type_) {
    arg = helpers_.boxValue(arg, arg->getType());
  }

  // Cast closure to array of pointers for field access
  llvm::Value* closurePtrs = builder_.CreatePointerCast(func,
                                                        llvm::PointerType::get(int8PtrType_, 0),
                                                        "closure_ptrs");

  // Load function pointer (field 0)
  llvm::Value* fnSlot = builder_.CreateConstGEP1_32(int8PtrType_, closurePtrs, 0, "fn_slot");
  llvm::Value* fnPtrRaw = builder_.CreateLoad(int8PtrType_, fnSlot, "fn_ptr_raw");

  // Load environment pointer (field 1)
  llvm::Value* envSlot = builder_.CreateConstGEP1_32(int8PtrType_, closurePtrs, 1, "env_slot");
  llvm::Value* envPtr = builder_.CreateLoad(int8PtrType_, envSlot, "env_ptr");

  // Cast function pointer to correct signature
  // All closures use uniform signature: void* fn(void* env, void* arg)
  std::vector<llvm::Type*> paramTypes = {int8PtrType_, int8PtrType_};
  llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, paramTypes, false);

  llvm::Value* funcPtr = builder_.CreatePointerCast(fnPtrRaw,
                                                    llvm::PointerType::get(funcType, 0),
                                                    "func_ptr");

  // Call with environment and argument
  return builder_.CreateCall(funcType, funcPtr, {envPtr, arg}, "call");
}

llvm::Value* ClosureConverter::genLet(const Let& let) {
  // Let binding: pattern <- value; body
  // Currently supports simple patterns (variable, wildcard)
  // Complex patterns (ADT destructuring) require full pattern compiler

  if (!exprGen_) {
    helpers_.emitError("Expression generator not initialized");
    return nullptr;
  }

  llvm::Value* value = exprGen_->genExpr(*let.value);
  if (!value) {
    return nullptr;
  }

  // Pattern binding
  if (auto* varPat = std::get_if<VarPat>(&let.pattern->node)) {
    symbols_.insert(varPat->name, value);
  } else if (std::get_if<WildcardPat>(&let.pattern->node)) {
    // Wildcard: value computed for side effects, not bound
  } else {
    // Complex patterns need destructuring infrastructure
    helpers_.emitError("Complex patterns in let bindings require destructuring support");
  }

  return exprGen_->genExpr(*let.body);
}

llvm::Value* ClosureConverter::genMatch(const Match& match) {
  // Pattern matching compilation with comprehensive pattern support
  // Generates control flow graph with basic blocks for each arm
  // Uses phi nodes to merge results from different arms
  //
  // Supported patterns:
  // - Variable: always matches, binds value
  // - Wildcard: always matches, discards value
  // - Constructor: ADT pattern with tag checking
  // - Cons: List pattern (head :: tail)
  // - List: Empty list pattern []
  // - Literal: Constant pattern (42, "hello", etc.)
  //
  // Pattern matching order:
  // Arms checked sequentially, first match wins
  // Variable/wildcard patterns catch all remaining cases

  if (!exprGen_) {
    helpers_.emitError("Expression generator not initialized");
    return nullptr;
  }

  llvm::Value* scrutinee = exprGen_->genExpr(*match.scrutinee);
  if (!scrutinee || match.arms.empty()) {
    return nullptr;
  }

  llvm::Function* function = builder_.GetInsertBlock()->getParent();
  llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(context_, "match_merge");

  llvm::Type* resultType = nullptr;
  llvm::PHINode* phi = nullptr;

  for (size_t i = 0; i < match.arms.size(); ++i) {
    const auto& arm = match.arms[i];
    llvm::BasicBlock* armBB = llvm::BasicBlock::Create(context_, "match_arm", function);
    llvm::BasicBlock* nextBB = (i + 1 < match.arms.size())
                                   ? llvm::BasicBlock::Create(context_, "match_next", function)
                                   : mergeBB;

    const Pattern& pattern = *arm.first;

    if (auto* varPat = std::get_if<VarPat>(&pattern.node)) {
      // Variable pattern: always matches, binds scrutinee to name
      builder_.CreateBr(armBB);
      builder_.SetInsertPoint(armBB);

      symbols_.enterScope();
      symbols_.insert(varPat->name, scrutinee);
      llvm::Value* result = exprGen_->genExpr(*arm.second);
      symbols_.exitScope();

      if (result) {
        if (!resultType) {
          resultType = result->getType();
          function->insert(function->end(), mergeBB);
          builder_.SetInsertPoint(mergeBB);
          phi = builder_.CreatePHI(resultType, match.arms.size(), "match_result");
          builder_.SetInsertPoint(armBB);
        }
        phi->addIncoming(result, builder_.GetInsertBlock());
      }

      builder_.CreateBr(mergeBB);
      break;  // Variable pattern terminates matching (catches all)
    } else if (std::get_if<WildcardPat>(&pattern.node)) {
      // Wildcard pattern: always matches, discards value
      builder_.CreateBr(armBB);
      builder_.SetInsertPoint(armBB);

      llvm::Value* result = exprGen_->genExpr(*arm.second);

      if (result) {
        if (!resultType) {
          resultType = result->getType();
          function->insert(function->end(), mergeBB);
          builder_.SetInsertPoint(mergeBB);
          phi = builder_.CreatePHI(resultType, match.arms.size(), "match_result");
          builder_.SetInsertPoint(armBB);
        }
        phi->addIncoming(result, builder_.GetInsertBlock());
      }

      builder_.CreateBr(mergeBB);
      break;  // Wildcard terminates matching
    } else if (auto* consPat = std::get_if<ConsPat>(&pattern.node)) {
      if (consPat->constructor == "::" && consPat->args.size() == 2) {
        // Cons pattern: (head :: tail)
        // Check if list is non-empty (scrutinee != null)
        // Extract head and tail using runtime functions
        llvm::Value* isNull = builder_.CreateICmpEQ(
            scrutinee,
            llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)),
            "is_empty");

        builder_.CreateCondBr(isNull, nextBB, armBB);
        builder_.SetInsertPoint(armBB);

        // Extract head and tail via runtime functions
        llvm::Value* head = builder_.CreateCall(runtime_.headFunc, {scrutinee}, "head");
        llvm::Value* tail = builder_.CreateCall(runtime_.tailFunc, {scrutinee}, "tail");

        symbols_.enterScope();

        // Bind pattern variables
        if (auto* headVar = std::get_if<VarPat>(&consPat->args[0]->node)) {
          symbols_.insert(headVar->name, head);
        }

        if (auto* tailVar = std::get_if<VarPat>(&consPat->args[1]->node)) {
          symbols_.insert(tailVar->name, tail);
        }

        llvm::Value* result = exprGen_->genExpr(*arm.second);
        symbols_.exitScope();

        if (result) {
          if (!resultType) {
            resultType = result->getType();
            function->insert(function->end(), mergeBB);
            llvm::BasicBlock* savedBB = builder_.GetInsertBlock();
            builder_.SetInsertPoint(mergeBB);
            phi = builder_.CreatePHI(resultType, match.arms.size(), "match_result");
            builder_.SetInsertPoint(savedBB);
          }
          phi->addIncoming(result, builder_.GetInsertBlock());
        }

        builder_.CreateBr(mergeBB);
        builder_.SetInsertPoint(nextBB);
      } else {
        // Generic ADT constructor pattern: Ctor arg1 arg2 ...
        // Check constructor tag matches, then extract fields
        //
        // ADT memory layout: [tag (i64), field1, field2, ...]
        // Tag checking enables pattern matching on different constructors
        auto tagIt = constructorTags_.find(consPat->constructor);
        if (tagIt != constructorTags_.end()) {
          int expectedTag = tagIt->second;

          // Null check for safety
          llvm::Value* isNull = builder_.CreateICmpEQ(
              scrutinee,
              llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)),
              "is_null");

          llvm::BasicBlock* checkTagBB = llvm::BasicBlock::Create(context_, "check_tag", function);
          builder_.CreateCondBr(isNull, nextBB, checkTagBB);
          builder_.SetInsertPoint(checkTagBB);

          // Load tag from ADT (offset 0)
          llvm::Value* tagPtr = builder_.CreatePointerCast(scrutinee,
                                                           llvm::PointerType::get(int64Type_, 0));
          llvm::Value* tag = builder_.CreateLoad(int64Type_, tagPtr, "tag");

          // Compare tag with expected constructor tag
          llvm::Value* tagMatch = builder_.CreateICmpEQ(
              tag, llvm::ConstantInt::get(int64Type_, expectedTag), "tag_match");
          builder_.CreateCondBr(tagMatch, armBB, nextBB);
          builder_.SetInsertPoint(armBB);

          // Extract and bind constructor arguments
          symbols_.enterScope();
          for (size_t i = 0; i < consPat->args.size(); ++i) {
            if (auto* varPat = std::get_if<VarPat>(&consPat->args[i]->node)) {
              // Load field at offset (1 + i) * 8 bytes
              // ADT layout: [tag(8), field0(8), field1(8), ...]
              llvm::Value* offset = llvm::ConstantInt::get(int64Type_, (1 + i) * 8);
              llvm::Value* fieldPtr =
                  builder_.CreateGEP(int8PtrType_, scrutinee, offset, "fieldptr");
              llvm::Value* fieldVal = builder_.CreateLoad(
                  int8PtrType_,
                  builder_.CreatePointerCast(fieldPtr, llvm::PointerType::get(int8PtrType_, 0)),
                  "fieldval");

              symbols_.insert(varPat->name, fieldVal);
            }
          }

          llvm::Value* result = exprGen_->genExpr(*arm.second);
          symbols_.exitScope();

          if (result) {
            if (!resultType) {
              resultType = result->getType();
              function->insert(function->end(), mergeBB);
              llvm::BasicBlock* savedBB = builder_.GetInsertBlock();
              builder_.SetInsertPoint(mergeBB);
              phi = builder_.CreatePHI(resultType, match.arms.size(), "match_result");
              builder_.SetInsertPoint(savedBB);
            }
            phi->addIncoming(result, builder_.GetInsertBlock());
          }

          builder_.CreateBr(mergeBB);
          builder_.SetInsertPoint(nextBB);
        }
      }
    } else if (auto* listPat = std::get_if<ListPat>(&pattern.node)) {
      // List pattern: currently only supports empty list []
      // Non-empty list patterns would need recursive matching
      if (listPat->elements.empty()) {
        // Empty list check: scrutinee == null
        llvm::Value* isNull = builder_.CreateICmpEQ(
            scrutinee,
            llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)),
            "is_empty");

        builder_.CreateCondBr(isNull, armBB, nextBB);
        builder_.SetInsertPoint(armBB);

        llvm::Value* result = exprGen_->genExpr(*arm.second);

        if (result) {
          if (!resultType) {
            resultType = result->getType();
            function->insert(function->end(), mergeBB);
            llvm::BasicBlock* savedBB = builder_.GetInsertBlock();
            builder_.SetInsertPoint(mergeBB);
            phi = builder_.CreatePHI(resultType, match.arms.size(), "match_result");
            builder_.SetInsertPoint(savedBB);
          }
          phi->addIncoming(result, builder_.GetInsertBlock());
        }

        builder_.CreateBr(mergeBB);
        builder_.SetInsertPoint(nextBB);
      }
    } else if (auto* litPat = std::get_if<LitPat>(&pattern.node)) {
      // Literal pattern: matches specific constant value
      // Uses appropriate comparison based on type (integer, float, string)

      // Generate literal constant for comparison
      llvm::Value* patternVal = exprGen_->genLit(Lit{litPat->value});
      if (!patternVal)
        continue;

      llvm::Value* matches = nullptr;
      if (scrutinee->getType()->isIntegerTy()) {
        matches = builder_.CreateICmpEQ(scrutinee, patternVal, "lit_eq");
      } else if (scrutinee->getType()->isDoubleTy()) {
        matches = builder_.CreateFCmpOEQ(scrutinee, patternVal, "lit_eq");
      } else if (scrutinee->getType()->isPointerTy()) {
        // String comparison uses runtime function (structural equality)
        matches = builder_.CreateCall(runtime_.stringEqFunc, {scrutinee, patternVal}, "str_eq");
      }

      if (matches) {
        builder_.CreateCondBr(matches, armBB, nextBB);
        builder_.SetInsertPoint(armBB);

        llvm::Value* result = exprGen_->genExpr(*arm.second);

        if (result) {
          if (!resultType) {
            resultType = result->getType();
            function->insert(function->end(), mergeBB);
            llvm::BasicBlock* savedBB = builder_.GetInsertBlock();
            builder_.SetInsertPoint(mergeBB);
            phi = builder_.CreatePHI(resultType, match.arms.size(), "match_result");
            builder_.SetInsertPoint(savedBB);
          }
          phi->addIncoming(result, builder_.GetInsertBlock());
        }

        builder_.CreateBr(mergeBB);
        builder_.SetInsertPoint(nextBB);
      }
    }
  }

  // Ensure merge block is in function
  if (!mergeBB->getParent()) {
    function->insert(function->end(), mergeBB);
  }
  builder_.SetInsertPoint(mergeBB);

  // Return phi node result or default value
  if (phi) {
    return phi;
  }

  return llvm::ConstantInt::get(int64Type_, 0);
}

std::set<std::string> ClosureConverter::findFreeVariables(const Expr& expr,
                                                          const std::set<std::string>& boundVars) {
  // Free variable analysis for closure conversion
  // A variable is free if:
  // 1. Referenced in expression
  // 2. Not bound by lambda parameter, let binding, or pattern match
  //
  // Algorithm: Traverse expression tree, tracking bound variables
  // Result: Set of variables needing capture in closure environment
  //
  // Used to determine what to pack into closure environment:
  // closure = { fn_ptr, env: [free_var1, free_var2, ...] }

  return std::visit(
      [&](const auto& node) -> std::set<std::string> {
        using T = std::decay_t<decltype(node)>;
        std::set<std::string> freeVars;

        if constexpr (std::is_same_v<T, Var>) {
          if (boundVars.find(node.name) == boundVars.end()) {
            freeVars.insert(node.name);
          }
        } else if constexpr (std::is_same_v<T, Lambda>) {
          std::set<std::string> newBound = boundVars;
          for (const auto& param : node.params) {
            collectPatternVars(*param, newBound);
          }
          return findFreeVariables(*node.body, newBound);
        } else if constexpr (std::is_same_v<T, App>) {
          std::set<std::string> fv1 = findFreeVariables(*node.func, boundVars);
          std::set<std::string> fv2 = findFreeVariables(*node.arg, boundVars);
          freeVars.insert(fv1.begin(), fv1.end());
          freeVars.insert(fv2.begin(), fv2.end());
        } else if constexpr (std::is_same_v<T, Let>) {
          std::set<std::string> fvVal = findFreeVariables(*node.value, boundVars);
          freeVars.insert(fvVal.begin(), fvVal.end());

          std::set<std::string> newBound = boundVars;
          collectPatternVars(*node.pattern, newBound);
          std::set<std::string> fvBody = findFreeVariables(*node.body, newBound);
          freeVars.insert(fvBody.begin(), fvBody.end());
        } else if constexpr (std::is_same_v<T, Match>) {
          std::set<std::string> fvScrut = findFreeVariables(*node.scrutinee, boundVars);
          freeVars.insert(fvScrut.begin(), fvScrut.end());
          for (const auto& arm : node.arms) {
            std::set<std::string> newBound = boundVars;
            collectPatternVars(*arm.first, newBound);
            std::set<std::string> fvArm = findFreeVariables(*arm.second, newBound);
            freeVars.insert(fvArm.begin(), fvArm.end());
          }
        } else if constexpr (std::is_same_v<T, If>) {
          std::set<std::string> fv1 = findFreeVariables(*node.cond, boundVars);
          std::set<std::string> fv2 = findFreeVariables(*node.thenBranch, boundVars);
          std::set<std::string> fv3 = findFreeVariables(*node.elseBranch, boundVars);
          freeVars.insert(fv1.begin(), fv1.end());
          freeVars.insert(fv2.begin(), fv2.end());
          freeVars.insert(fv3.begin(), fv3.end());
        } else if constexpr (std::is_same_v<T, BinOp>) {
          std::set<std::string> fv1 = findFreeVariables(*node.left, boundVars);
          std::set<std::string> fv2 = findFreeVariables(*node.right, boundVars);
          freeVars.insert(fv1.begin(), fv1.end());
          freeVars.insert(fv2.begin(), fv2.end());
        } else if constexpr (std::is_same_v<T, List>) {
          for (const auto& elem : node.elements) {
            std::set<std::string> fv = findFreeVariables(*elem, boundVars);
            freeVars.insert(fv.begin(), fv.end());
          }
        } else if constexpr (std::is_same_v<T, Record>) {
          for (const auto& [name, expr] : node.fields) {
            std::set<std::string> fv = findFreeVariables(*expr, boundVars);
            freeVars.insert(fv.begin(), fv.end());
          }
        } else if constexpr (std::is_same_v<T, RecordAccess>) {
          return findFreeVariables(*node.record, boundVars);
        } else if constexpr (std::is_same_v<T, RecordUpdate>) {
          std::set<std::string> fv = findFreeVariables(*node.record, boundVars);
          freeVars.insert(fv.begin(), fv.end());
          for (const auto& [name, expr] : node.updates) {
            std::set<std::string> fvUpd = findFreeVariables(*expr, boundVars);
            freeVars.insert(fvUpd.begin(), fvUpd.end());
          }
        } else if constexpr (std::is_same_v<T, Block>) {
          std::set<std::string> currentBound = boundVars;
          for (const auto& stmt : node.stmts) {
            std::set<std::string> fv = findFreeVariables(*stmt, currentBound);
            freeVars.insert(fv.begin(), fv.end());
            if (auto* let = std::get_if<Let>(&stmt->node)) {
              collectPatternVars(*let->pattern, currentBound);
            }
          }
        }
        return freeVars;
      },
      expr.node);
}

llvm::Value* ClosureConverter::createClosure(llvm::Function* func,
                                             const std::vector<llvm::Value*>& envValues) {
  // Closure construction: allocates structure and packs captured values
  // Layout: { fn_ptr: void*, env_ptr: void* }
  //
  // Environment layout (if non-empty): [val1, val2, ..., valN]
  // Each captured variable stored as boxed value (void*)
  //
  // Design rationale:
  // - Uniform calling convention enables higher-order functions
  // - Environment stored separately allows efficient closure creation
  // - All closures have same type regardless of captured values

  llvm::Value* closureSize = llvm::ConstantInt::get(int64Type_, 16);
  llvm::Value* closure = gcHelpers_.createGCAlloc(closureSize, ObjectTag::Closure);

  llvm::Value* closurePtrs = builder_.CreatePointerCast(closure,
                                                        llvm::PointerType::get(int8PtrType_, 0));

  // Store function pointer (field 0)
  llvm::Value* funcPtr = builder_.CreatePointerCast(func, int8PtrType_);
  llvm::Value* fnSlot = builder_.CreateConstGEP1_32(int8PtrType_, closurePtrs, 0);
  builder_.CreateStore(funcPtr, fnSlot);

  // Create and store environment (field 1)
  llvm::Value* envPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_));

  if (!envValues.empty()) {
    // Allocate environment array
    llvm::Value* envSize = llvm::ConstantInt::get(int64Type_, envValues.size() * 8);
    envPtr = gcHelpers_.createGCAlloc(envSize, ObjectTag::Environment);

    llvm::Value* envPtrs = builder_.CreatePointerCast(envPtr,
                                                      llvm::PointerType::get(int8PtrType_, 0));

    // Pack captured values into environment
    for (size_t i = 0; i < envValues.size(); ++i) {
      llvm::Value* slot = builder_.CreateConstGEP1_32(int8PtrType_, envPtrs, i);
      llvm::Value* val = envValues[i];

      // Box primitive values for uniform storage
      if (val->getType() == int64Type_) {
        val = helpers_.boxValue(val, val->getType());
      }
      builder_.CreateStore(val, slot);
    }
  }

  // Store environment pointer
  llvm::Value* envSlot = builder_.CreateConstGEP1_32(int8PtrType_, closurePtrs, 1);
  builder_.CreateStore(envPtr, envSlot);

  return closure;
}

llvm::Value* ClosureConverter::genLambdaImpl(const std::vector<PatternPtr>& params,
                                             const ExprPtr& body) {
  // Lambda implementation entry point
  // Delegates to slice version starting at parameter 0
  return genLambdaImplSlice(params, 0, body);
}

llvm::Value* ClosureConverter::genLambdaImplSlice(const std::vector<PatternPtr>& params,
                                                  size_t startIdx,
                                                  const ExprPtr& body) {
  // Curried lambda implementation with closure conversion
  // Transforms multi-parameter lambdas into chain of single-parameter functions
  //
  // Example: \x y z -> body
  // Becomes: \x -> \y -> \z -> body
  //
  // Each function captures remaining parameters in closure
  // Enables partial application: (f 1) returns closure waiting for y, z

  if (!exprGen_) {
    helpers_.emitError("Expression generator not initialized");
    return nullptr;
  }

  if (startIdx >= params.size()) {
    // Base case: all parameters processed, generate body
    return exprGen_->genExpr(*body);
  }

  // Process parameter at startIdx
  const PatternPtr& param = params[startIdx];

  // Find bound variables from all remaining parameters
  std::set<std::string> boundVars;
  for (size_t i = startIdx; i < params.size(); ++i) {
    collectPatternVars(*params[i], boundVars);
  }

  // Find free variables in body (excluding parameters)
  std::set<std::string> freeVars = findFreeVariables(*body, boundVars);

  // Collect values for captured variables
  std::vector<llvm::Value*> capturedValues;
  std::vector<std::string> capturedNames;

  for (const auto& var : freeVars) {
    if (symbols_.contains(var)) {
      capturedValues.push_back(symbols_.lookup(var));
      capturedNames.push_back(var);
    }
  }

  // Generate function with uniform signature: void* fn(void* env, void* arg)
  std::vector<llvm::Type*> paramTypes = {int8PtrType_, int8PtrType_};
  llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, paramTypes, false);

  static int lambdaCount = 0;
  std::string funcName = "lambda_" + std::to_string(lambdaCount++);

  llvm::Function* function =
      llvm::Function::Create(funcType, llvm::Function::InternalLinkage, funcName, &module_);

  // Add optimization hints
  function->addFnAttr(llvm::Attribute::NoUnwind);

  // Small lambdas get always inline for performance
  if (capturedNames.size() <= 3) {
    function->addFnAttr(llvm::Attribute::AlwaysInline);
  } else {
    function->addFnAttr(llvm::Attribute::InlineHint);
  }

  // Save current insert point
  llvm::BasicBlock* savedBlock = builder_.GetInsertBlock();

  // Generate function body
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context_, "entry", function);
  builder_.SetInsertPoint(entry);
  symbols_.enterScope();

  // Unpack environment
  llvm::Value* envArg = function->getArg(0);
  envArg->setName("env");
  llvm::Value* argArg = function->getArg(1);
  argArg->setName("arg");

  if (!capturedNames.empty()) {
    llvm::Value* envPtrs = builder_.CreatePointerCast(envArg,
                                                      llvm::PointerType::get(int8PtrType_, 0));
    for (size_t i = 0; i < capturedNames.size(); ++i) {
      llvm::Value* slot = builder_.CreateConstGEP1_32(int8PtrType_, envPtrs, i);
      llvm::Value* val = builder_.CreateLoad(int8PtrType_, slot);
      symbols_.insert(capturedNames[i], val);
    }
  }

  // Bind argument to parameter pattern
  if (auto* varPat = std::get_if<VarPat>(&param->node)) {
    symbols_.insert(varPat->name, argArg);
  }

  // Generate inner body (either next parameter or final body)
  llvm::Value* bodyVal;
  if (startIdx + 1 < params.size()) {
    // More parameters: recurse to create nested closure
    bodyVal = genLambdaImplSlice(params, startIdx + 1, body);
  } else {
    // Last parameter: generate body expression
    bodyVal = exprGen_->genExpr(*body);
  }

  if (bodyVal) {
    // Auto-box return value for uniform calling convention
    if (bodyVal->getType()->isIntegerTy() && bodyVal->getType() != int1Type_) {
      bodyVal = helpers_.boxValue(bodyVal, bodyVal->getType());
    }
    builder_.CreateRet(bodyVal);
  } else {
    builder_.CreateRet(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)));
  }

  symbols_.exitScope();

  // Restore insert point
  if (savedBlock) {
    builder_.SetInsertPoint(savedBlock);
  }

  // Create closure with captured environment
  return createClosure(function, capturedValues);
}

}  // namespace solis
