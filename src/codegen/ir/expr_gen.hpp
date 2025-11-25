// Solis Programming Language - Expression IR Generation
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Generates LLVM IR for all Solis expression types
// Handles literals, variables, operators, control flow, and data structures

#pragma once

#include "codegen/gc/gc_helpers.hpp"
#include "codegen/ir/helpers.hpp"
#include "codegen/ir/types.hpp"
#include "codegen/runtime/runtime_init.hpp"
#include "parser/ast.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Value.h>

#include <map>
#include <string>

namespace solis {

// Forward declarations
class SymbolTable;
class ClosureConverter;

// Expression IR Generator
// Converts AST expressions to LLVM IR instructions
// Maintains execution context and handles value representation
class ExprGenerator {
public:
  ExprGenerator(llvm::LLVMContext& context,
                llvm::Module& module,
                llvm::IRBuilder<>& builder,
                SymbolTable& symbols,
                TypeConverter& types,
                IRHelpers& helpers,
                GCHelpers& gcHelpers,
                RuntimeFunctions& runtime,
                std::map<std::string, int>& constructorTags);

  // Main expression dispatcher
  // Routes expression nodes to specialized generation methods
  llvm::Value* genExpr(const Expr& expr);

  // Variable reference generation
  // Resolves name through symbol table, auto-calls zero-parameter functions
  llvm::Value* genVar(const Var& var);

  // Literal value generation
  // Handles: integers, floats, booleans, strings, BigInt
  llvm::Value* genLit(const Lit& lit);

  // Binary operator generation
  // Handles: arithmetic (+, -, *, /, %), comparison (==, !=, <, >, <=, >=),
  // logical (&&, ||), string concatenation (++)
  llvm::Value* genBinOp(const BinOp& binOp);

  // Conditional expression generation
  // Generates control flow with phi nodes for result merging
  llvm::Value* genIf(const If& ifExpr);

  // Block expression generation
  // Evaluates sequence of statements, returns last value
  llvm::Value* genBlock(const Block& block);

  // Strict evaluation expression
  // Forces immediate evaluation (no thunk wrapping)
  llvm::Value* genStrict(const Strict& strict);

  // Bind expression generation (monadic bind)
  // Used in do-notation for sequencing effects
  llvm::Value* genBind(const Bind& bind);

  // List literal generation
  // Builds cons cells from right to left
  llvm::Value* genList(const List& list);

  // Record literal generation
  // Allocates record with field names and values
  llvm::Value* genRecord(const Record& record);

  // Record field access generation
  // Runtime field lookup by name with loop-based search
  llvm::Value* genRecordAccess(const RecordAccess& access);

  // Record update generation (immutable)
  // Copy-on-write: allocate new record, copy old values, update fields
  llvm::Value* genRecordUpdate(const RecordUpdate& update);

  // Set closure converter for lambda/app/let/match delegation
  void setClosureConverter(ClosureConverter* closureConv) { closureConv_ = closureConv; }

private:
  llvm::LLVMContext& context_;
  llvm::Module& module_;
  llvm::IRBuilder<>& builder_;
  SymbolTable& symbols_;
  TypeConverter& types_;
  IRHelpers& helpers_;
  GCHelpers& gcHelpers_;
  RuntimeFunctions& runtime_;
  std::map<std::string, int>& constructorTags_;

  // Closure converter for complex expressions (circular dependency resolution)
  ClosureConverter* closureConv_ = nullptr;

  // Cached common types for efficient access
  llvm::Type* int1Type_;
  llvm::Type* int64Type_;
  llvm::Type* doubleType_;
  llvm::Type* int8PtrType_;
};

}  // namespace solis
