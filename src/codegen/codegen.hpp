// Solis Programming Language - Codegen Header
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#pragma once

#include "codegen/backend/target_backend.hpp"
#include "codegen/gc/gc_helpers.hpp"
#include "codegen/gc/gc_support.hpp"
#include "codegen/ir/closure.hpp"
#include "codegen/ir/decl_gen.hpp"
#include "codegen/ir/expr_gen.hpp"
#include "codegen/ir/helpers.hpp"
#include "codegen/ir/types.hpp"
#include "codegen/runtime/runtime_init.hpp"
#include "codegen/support/diagnostics.hpp"
#include "codegen/support/value_repr.hpp"
#include "parser/ast.hpp"
#include "type/typer.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace solis {

// Forward declarations
class CodeGen;

// Symbol table for tracking variables and their LLVM values
class SymbolTable {
private:
  std::vector<std::map<std::string, llvm::Value*>> scopes_;

public:
  SymbolTable();

  void enterScope();
  void exitScope();

  void insert(const std::string& name, llvm::Value* value);
  llvm::Value* lookup(const std::string& name) const;
  bool contains(const std::string& name) const;
};

// RAII wrapper for automatic scope management
// Ensures enterScope/exitScope are always balanced
class ScopeGuard {
private:
  SymbolTable& symbols_;
  bool dismissed_ = false;

public:
  explicit ScopeGuard(SymbolTable& symbols)
      : symbols_(symbols) {
    symbols_.enterScope();
  }

  ~ScopeGuard() {
    if (!dismissed_) {
      symbols_.exitScope();
    }
  }

  // Prevent copying
  ScopeGuard(const ScopeGuard&) = delete;
  ScopeGuard& operator=(const ScopeGuard&) = delete;

  // Allow moving
  ScopeGuard(ScopeGuard&& other) noexcept
      : symbols_(other.symbols_)
      , dismissed_(other.dismissed_) {
    other.dismissed_ = true;
  }

  // Manually dismiss guard (useful for error paths)
  void dismiss() { dismissed_ = true; }
};

// LLVM Code Generator for Solis
class CodeGen {
private:
  // LLVM core components
  std::unique_ptr<llvm::LLVMContext> context_;
  std::unique_ptr<llvm::Module> module_;
  std::unique_ptr<llvm::IRBuilder<>> builder_;

  // Symbol table for variable bindings
  SymbolTable symbols_;

  // Diagnostic engine for professional error reporting
  DiagnosticEngine& diags_;

  // Value representation strategy
  ValueRepresentationStrategy valueRepr_;

  // Constructor tag mapping for ADTs
  std::map<std::string, int> constructorTags_;

  // Record field order tracking: variable name -> field names in order
  // Enables correct field index lookup during record access
  std::map<std::string, std::vector<std::string>> recordFieldOrder_;

  // Trait method registry for type class dispatch
  // Maps trait names to method signatures for vtable construction
  // TraitMethodInfo defined in decl_gen.hpp
  std::map<std::string, std::vector<TraitMethodInfo>> traitRegistry_;

  // Implementation vtables for trait-based dispatch
  // Maps "TraitName_TypeName" to list of implementing functions
  std::map<std::string, std::vector<llvm::Function*>> implVTables_;

  // Type cache for common LLVM types
  llvm::Type* voidType_;
  llvm::Type* int1Type_;     // i1 (bool)
  llvm::Type* int64Type_;    // i64 (int)
  llvm::Type* doubleType_;   // double (float)
  llvm::Type* int8PtrType_;  // i8* (string, pointers)

  // Runtime function registry
  // Manages all runtime function declarations and initialization
  RuntimeFunctions runtime_;

  // Current function being compiled (for return statements)
  llvm::Function* currentFunction_;

  // Type inference engine (for type-directed compilation)
  TypeInference* typeInference_;

  // Debug options
  bool debugMode_ = false;

  // Closure conversion helpers
  std::set<std::string> findFreeVariables(const Expr& expr, const std::set<std::string>& boundVars);
  llvm::Value* createClosure(llvm::Function* func, const std::vector<llvm::Value*>& envValues);
  llvm::Value* genLambdaImpl(const std::vector<PatternPtr>& params, const ExprPtr& body);
  llvm::Value* genLambdaImplSlice(const std::vector<PatternPtr>& params,
                                  size_t startIdx,
                                  const ExprPtr& body);

public:
  CodeGen(const std::string& moduleName, DiagnosticEngine& diags);
  ~CodeGen();

  // Main compilation entry points
  void compileModule(const Module& module);
  void compileDecl(const Decl& decl);

  // Get generated module
  llvm::Module* getModule() { return module_.get(); }
  llvm::LLVMContext& getContext() { return *context_; }

  // Set type inference engine
  void setTypeInference(TypeInference* ti) { typeInference_ = ti; }

  // Enable/disable debug mode
  void setDebugMode(bool enable) { debugMode_ = enable; }

  // Verify and print module IR (for debugging)
  bool verifyModule();
  void printModule();

  // Emit LLVM IR to file (delegates to TargetBackend)
  void emitLLVM(const std::string& filename);

  // Emit object file (delegates to TargetBackend)
  void emitObject(const std::string& filename);

  // Emit executable (delegates to TargetBackend)
  void emitExecutable(const std::string& filename, int optLevel = 2);

private:
  // Modular IR generation components (initialized after runtime)
  TypeConverter typeConverter_;
  IRHelpers irHelpers_;
  GCHelpers gcHelpers_;
  ExprGenerator exprGen_;
  ClosureConverter closureConv_;
  DeclCompiler declCompiler_;

  // Target backend for code emission
  TargetBackend backend_;

private:
  // Initialize runtime function declarations
  void initializeRuntime();

  // Type conversion from Solis to LLVM types
  llvm::Type* toLLVMType(const InferTypePtr& type);
  llvm::Type* getValueType();    // Generic value type (boxed)
  llvm::Type* getThunkType();    // Thunk structure type
  llvm::Type* getClosureType();  // Closure structure type
  llvm::Type* getConsType();     // Cons cell type for lists

  // Expression code generation
  llvm::Value* genExpr(const Expr& expr);
  llvm::Value* genVar(const Var& var);
  llvm::Value* genLit(const Lit& lit);
  llvm::Value* genLambda(const Lambda& lambda, const std::string& recursiveName = "");
  llvm::Value* genApp(const App& app);
  llvm::Value* genLet(const Let& let);
  llvm::Value* genMatch(const Match& match);
  llvm::Value* genIf(const If& ifExpr);
  llvm::Value* genBinOp(const BinOp& binOp);
  llvm::Value* genList(const List& list);
  llvm::Value* genRecord(const Record& record);
  llvm::Value* genRecordAccess(const RecordAccess& access);
  llvm::Value* genRecordUpdate(const RecordUpdate& update);
  llvm::Value* genBlock(const Block& block);
  llvm::Value* genStrict(const Strict& strict);
  llvm::Value* genBind(const Bind& bind);

  // Pattern matching compilation
  struct MatchContext {
    llvm::Value* scrutinee;
    llvm::BasicBlock* failBlock;
    std::map<std::string, llvm::Value*> bindings;
  };

  void genPattern(const Pattern& pattern, MatchContext& ctx);
  llvm::Value* genPatternMatch(const Pattern& pattern,
                               llvm::Value* value,
                               llvm::BasicBlock* successBlock,
                               llvm::BasicBlock* failBlock);

  // Helper functions
  llvm::Value* createThunk(llvm::Function* computeFn, llvm::Value* env);
  llvm::Value* forceValue(llvm::Value* value);
  llvm::Value* boxValue(llvm::Value* value, llvm::Type* type);
  llvm::Value* unboxValue(llvm::Value* boxed, llvm::Type* targetType);

  // Function compilation
  llvm::Function* genFunction(const FunctionDecl& funcDecl);
  llvm::Function* createClosureFunction(const Lambda& lambda,
                                        const std::vector<std::string>& freeVars);

  // Declaration compilation
  void genFunctionDecl(const FunctionDecl& funcDecl);
  void genTypeDecl(const TypeDecl& typeDecl);
  void genModuleDecl(const ModuleDecl& moduleDecl);
  void genImportDecl(const ImportDecl& importDecl);
  void genTraitDecl(const TraitDecl& traitDecl);
  void genImplDecl(const ImplDecl& implDecl);

  // ADT constructor generation with currying support
  void genCurriedConstructor(const std::string& ctorName, size_t arity, int tag);

  // Utility functions with GC tag support
  llvm::Value* createGCAlloc(llvm::Value* size, ObjectTag tag);
  llvm::Value* createGCAllocAtomic(llvm::Value* size, ObjectTag tag);
  llvm::Constant* createStringConstant(const std::string& str);

  // GC write barrier wrapper
  void emitWriteBarrier(llvm::Value* obj, llvm::Value* field, llvm::Value* value);

  // Diagnostic methods - replaces error() with error handling
  void emitError(const std::string& message);
  void emitWarning(const std::string& message);
  bool hasErrors() const { return diags_.hasErrors(); }

  // Helper: Create RAII scope guard
  ScopeGuard makeScope() { return ScopeGuard(symbols_); }
};

// Optimization passes
class OptimizerPipeline {
public:
  static void optimize(llvm::Module* module, int optLevel);

private:
  static void addStandardPasses(llvm::Module* module, int optLevel);
  static void addSolisSpecificPasses(llvm::Module* module);
};

}  // namespace solis
