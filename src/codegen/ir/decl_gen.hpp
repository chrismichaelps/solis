// Solis Programming Language - Declaration Compilation Module
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Compilation of all declaration types
// Handles functions, ADTs, traits, implementations, modules, imports
// Manages trait registry and vtable generation for dynamic dispatch

#pragma once

#include "parser/ast.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <map>
#include <string>
#include <vector>

namespace solis {

// Forward declarations
class SymbolTable;
class TypeConverter;
class IRHelpers;
class GCHelpers;
class ClosureConverter;
class RuntimeFunctions;

// Trait method metadata for vtable construction
struct TraitMethodInfo {
  std::string name;
  llvm::FunctionType* type;
};

// Declaration Compiler
// Compiles all top-level declarations: functions, types, traits, modules
// Manages trait system with registry and vtable generation
class DeclCompiler {
public:
  DeclCompiler(llvm::LLVMContext& context,
               llvm::Module& module,
               llvm::IRBuilder<>& builder,
               SymbolTable& symbols,
               TypeConverter& types,
               IRHelpers& helpers,
               GCHelpers& gcHelpers,
               RuntimeFunctions& runtime,
               std::map<std::string, int>& constructorTags,
               std::map<std::string, std::vector<TraitMethodInfo>>& traitRegistry,
               std::map<std::string, std::vector<llvm::Function*>>& implVTables);

  // Function declaration compilation
  // Generates curried functions for multi-parameter declarations
  // Handles zero-parameter functions as constant thunks
  void genFunctionDecl(const FunctionDecl& funcDecl);

  // Type declaration compilation (ADTs)
  // Generates constructors with proper currying
  // Assigns unique tags for pattern matching
  void genTypeDecl(const TypeDecl& typeDecl);

  // Module declaration
  // Records module name in IR metadata for debugging
  void genModuleDecl(const ModuleDecl& moduleDecl);

  // Import declaration
  // Handled by module resolver at compile time
  // No IR generation needed
  void genImportDecl(const ImportDecl& importDecl);

  // Trait declaration
  // Populates trait registry with method signatures
  // Enables vtable construction during impl generation
  void genTraitDecl(const TraitDecl& traitDecl);

  // Implementation declaration
  // Generates vtable for trait-based dynamic dispatch
  // Compiles trait methods with mangled names
  void genImplDecl(const ImplDecl& implDecl);

  // Set closure converter for function body generation
  // Resolves circular dependency with closure module
  void setClosureConverter(ClosureConverter* closureConv) { closureConv_ = closureConv; }

  // Set current function context (for nested declarations)
  void setCurrentFunction(llvm::Function* func) { currentFunction_ = func; }

private:
  // Curried constructor generation for multi-argument ADTs
  // Generates chain of closures: Ctor a b c becomes
  // \a -> \b -> \c -> ADT{tag, a, b, c}
  void genCurriedConstructor(const std::string& ctorName, size_t arity, int tag);

  llvm::LLVMContext& context_;
  llvm::Module& module_;
  llvm::IRBuilder<>& builder_;
  SymbolTable& symbols_;
  TypeConverter& types_;
  IRHelpers& helpers_;
  GCHelpers& gcHelpers_;
  RuntimeFunctions& runtime_;
  std::map<std::string, int>& constructorTags_;

  // Trait system state
  std::map<std::string, std::vector<TraitMethodInfo>>& traitRegistry_;
  std::map<std::string, std::vector<llvm::Function*>>& implVTables_;

  // Current compilation context
  ClosureConverter* closureConv_ = nullptr;
  llvm::Function* currentFunction_ = nullptr;

  // Cached common types
  llvm::Type* int1Type_;
  llvm::Type* int64Type_;
  llvm::Type* int8PtrType_;
};

}  // namespace solis
