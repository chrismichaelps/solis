// Solis Programming Language - Closure Conversion Module
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Lambda compilation and closure conversion
// Handles first-class functions, currying, and free variable capture
// Pattern matching compilation for Let and Match expressions

#pragma once

#include "parser/ast.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <map>
#include <set>
#include <string>
#include <vector>

namespace solis {

// Forward declarations
class SymbolTable;
class TypeConverter;
class IRHelpers;
class GCHelpers;
class RuntimeFunctions;
class ExprGenerator;

// Closure Conversion and Lambda Compilation
// Transforms lambdas into closure structures with captured environments
// Implements currying for multi-parameter functions
class ClosureConverter {
public:
  ClosureConverter(llvm::LLVMContext& context,
                   llvm::Module& module,
                   llvm::IRBuilder<>& builder,
                   SymbolTable& symbols,
                   TypeConverter& types,
                   IRHelpers& helpers,
                   GCHelpers& gcHelpers,
                   RuntimeFunctions& runtime,
                   std::map<std::string, int>& constructorTags);

  // Lambda expression compilation
  // Generates closure with captured free variables
  // recursiveName: reserved for future let rec syntax
  llvm::Value* genLambda(const Lambda& lambda, const std::string& recursiveName = "");

  // Function application
  // Unpacks closure, extracts function pointer and environment
  // Calls function with environment and argument
  llvm::Value* genApp(const App& app);

  // Let binding with pattern matching
  // Evaluates value, binds pattern, evaluates body
  llvm::Value* genLet(const Let& let);

  // Pattern matching expression
  // Multi-arm match with control flow and phi nodes
  llvm::Value* genMatch(const Match& match);

  // Set expression generator for body compilation
  // Resolves circular dependency between closures and expressions
  void setExprGenerator(ExprGenerator* exprGen) { exprGen_ = exprGen; }

  // Closure construction (public for DeclCompiler access)
  // Allocates closure structure: { fn_ptr, env_ptr }
  // Packs captured values into environment array
  llvm::Value* createClosure(llvm::Function* func, const std::vector<llvm::Value*>& envValues);

  // Lambda implementation helper (slice from startIdx)
  // Enables recursive currying by processing parameters from specific index
  // Made public for DeclCompiler function generation
  llvm::Value* genLambdaImplSlice(const std::vector<PatternPtr>& params,
                                  size_t startIdx,
                                  const ExprPtr& body);

private:
  // Free variable analysis for closure conversion
  // Computes set of variables referenced but not bound in expression
  // Required to determine what to capture in closure environment
  std::set<std::string> findFreeVariables(const Expr& expr, const std::set<std::string>& boundVars);

  // Lambda implementation generation (currying)
  // Generates nested functions for multi-parameter lambdas
  // Each function takes one parameter and returns closure for next
  llvm::Value* genLambdaImpl(const std::vector<PatternPtr>& params, const ExprPtr& body);

  llvm::LLVMContext& context_;
  llvm::Module& module_;
  llvm::IRBuilder<>& builder_;
  SymbolTable& symbols_;
  TypeConverter& types_;
  IRHelpers& helpers_;
  GCHelpers& gcHelpers_;
  RuntimeFunctions& runtime_;
  std::map<std::string, int>& constructorTags_;

  // Pointer to expression generator (circular dependency)
  // Set after construction to enable body generation
  ExprGenerator* exprGen_ = nullptr;

  // Cached common types
  llvm::Type* int1Type_;
  llvm::Type* int64Type_;
  llvm::Type* int8PtrType_;
};
}  // namespace solis
