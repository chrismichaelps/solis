// Solis Programming Language - Code Generator (Refactored with Delegation)
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License
//
// Orchestration layer - delegates to specialized modules
// Design: Facade pattern for clean API, components do the work

#include "codegen/codegen.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/TargetSelect.h>

namespace solis {

// SymbolTable Implementation

SymbolTable::SymbolTable() {
  scopes_.push_back({});
}

void SymbolTable::enterScope() {
  scopes_.push_back({});
}

void SymbolTable::exitScope() {
  if (scopes_.size() > 1) {
    scopes_.pop_back();
  }
}

void SymbolTable::insert(const std::string& name, llvm::Value* value) {
  scopes_.back()[name] = value;
}

llvm::Value* SymbolTable::lookup(const std::string& name) const {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return found->second;
    }
  }
  return nullptr;
}

bool SymbolTable::contains(const std::string& name) const {
  return lookup(name) != nullptr;
}

// CodeGen Implementation

CodeGen::CodeGen(const std::string& moduleName, DiagnosticEngine& diags)
    : context_(std::make_unique<llvm::LLVMContext>())
    , module_(std::make_unique<llvm::Module>(moduleName, *context_))
    , builder_(std::make_unique<llvm::IRBuilder<>>(*context_))
    , diags_(diags)
    , valueRepr_(ValueRepr::Hybrid)
    , currentFunction_(nullptr)
    , typeInference_(nullptr)
    ,
    // Initialize modular components - order matters!
    // 1. Basic converters first
    typeConverter_(*context_)
    , irHelpers_(*context_, *module_, *builder_, diags_)
    , gcHelpers_(*builder_, *context_, runtime_)
    ,
    // 2. Expression and closure generators (circular dependency handled below)
    exprGen_(*context_,
             *module_,
             *builder_,
             symbols_,
             typeConverter_,
             irHelpers_,
             gcHelpers_,
             runtime_,
             constructorTags_)
    , closureConv_(*context_,
                   *module_,
                   *builder_,
                   symbols_,
                   typeConverter_,
                   irHelpers_,
                   gcHelpers_,
                   runtime_,
                   constructorTags_)
    ,
    // 3. Declaration compiler
    declCompiler_(*context_,
                  *module_,
                  *builder_,
                  symbols_,
                  typeConverter_,
                  irHelpers_,
                  gcHelpers_,
                  runtime_,
                  constructorTags_,
                  traitRegistry_,
                  implVTables_)
    , backend_(diags) {
  // Initialize common types for backward compatibility
  voidType_ = llvm::Type::getVoidTy(*context_);
  int1Type_ = llvm::Type::getInt1Ty(*context_);
  int64Type_ = llvm::Type::getInt64Ty(*context_);
  doubleType_ = llvm::Type::getDoubleTy(*context_);
  int8PtrType_ = llvm::PointerType::get(*context_, 0);

  // Initialize runtime functions
  initializeRuntime();

  // Two-phase initialization: resolve circular dependency
  // ExprGenerator ↔ ClosureConverter
  exprGen_.setClosureConverter(&closureConv_);
  closureConv_.setExprGenerator(&exprGen_);
  declCompiler_.setClosureConverter(&closureConv_);

  // Initialize LLVM targets
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
}

CodeGen::~CodeGen() = default;

void CodeGen::initializeRuntime() {
  // Runtime initialization with error handling
  // All runtime functions declared with proper attributes for optimization

  // Allocation functions with GC tag support
  llvm::FunctionType* allocType =
      llvm::FunctionType::get(int8PtrType_, {int64Type_, llvm::Type::getInt8Ty(*context_)}, false);

  runtime_.allocFunc = module_->getOrInsertFunction("solis_alloc", allocType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.allocFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addRetAttr(llvm::Attribute::NoAlias);
  }

  runtime_.allocAtomicFunc = module_->getOrInsertFunction("solis_alloc_atomic", allocType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.allocAtomicFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addRetAttr(llvm::Attribute::NoAlias);
  }

  // GC write barrier for generational GC
  llvm::FunctionType* writeBarrierType =
      llvm::FunctionType::get(voidType_, {int8PtrType_, int8PtrType_, int8PtrType_}, false);
  runtime_.gcWriteBarrierFunc = module_->getOrInsertFunction("solis_gc_write_barrier",
                                                             writeBarrierType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.gcWriteBarrierFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
  }

  // Thunk support for lazy evaluation
  llvm::FunctionType* thunkFnType = llvm::FunctionType::get(int8PtrType_, {int8PtrType_}, false);
  llvm::PointerType* thunkFnPtrType = llvm::PointerType::get(thunkFnType, 0);
  llvm::FunctionType* createThunkType = llvm::FunctionType::get(int8PtrType_,
                                                                {thunkFnPtrType, int8PtrType_},
                                                                false);
  runtime_.createThunkFunc = module_->getOrInsertFunction("solis_create_thunk", createThunkType);

  llvm::FunctionType* forceType = llvm::FunctionType::get(int8PtrType_, {int8PtrType_}, false);
  runtime_.forceThunkFunc = module_->getOrInsertFunction("solis_force", forceType);

  // String operations
  llvm::FunctionType* stringConcatType = llvm::FunctionType::get(int8PtrType_,
                                                                 {int8PtrType_, int8PtrType_},
                                                                 false);
  runtime_.stringConcatFunc = module_->getOrInsertFunction("solis_string_concat", stringConcatType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.stringConcatFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addParamAttr(0, llvm::Attribute::ReadOnly);
    F->addParamAttr(1, llvm::Attribute::ReadOnly);
  }

  llvm::FunctionType* stringEqType = llvm::FunctionType::get(int1Type_,
                                                             {int8PtrType_, int8PtrType_},
                                                             false);
  runtime_.stringEqFunc = module_->getOrInsertFunction("solis_string_eq", stringEqType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.stringEqFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
    F->addParamAttr(0, llvm::Attribute::ReadOnly);
    F->addParamAttr(1, llvm::Attribute::ReadOnly);
  }

  // I/O operations
  llvm::FunctionType* printType = llvm::FunctionType::get(int8PtrType_, {int8PtrType_}, false);
  runtime_.printFunc = module_->getOrInsertFunction("solis_print", printType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.printFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addParamAttr(0, llvm::Attribute::ReadOnly);
  }

  llvm::FunctionType* readLineType = llvm::FunctionType::get(int8PtrType_, {}, false);
  runtime_.readLineFunc = module_->getOrInsertFunction("solis_read_line", readLineType);

  // List operations
  runtime_.consFunc = module_->getOrInsertFunction(
      "solis_cons", llvm::FunctionType::get(int8PtrType_, {int8PtrType_, int8PtrType_}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.consFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
  }

  runtime_.headFunc = module_->getOrInsertFunction(
      "solis_head", llvm::FunctionType::get(int8PtrType_, {int8PtrType_}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.headFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
  }

  runtime_.tailFunc = module_->getOrInsertFunction(
      "solis_tail", llvm::FunctionType::get(int8PtrType_, {int8PtrType_}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.tailFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
  }

  runtime_.lengthFunc = module_->getOrInsertFunction(
      "solis_list_length", llvm::FunctionType::get(int64Type_, {int8PtrType_}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(runtime_.lengthFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
  }

  // Register built-in functions in global symbol table
  symbols_.insert("print", llvm::cast<llvm::Function>(runtime_.printFunc.getCallee()));
  symbols_.insert("readLine", llvm::cast<llvm::Function>(runtime_.readLineFunc.getCallee()));
  symbols_.insert("head", llvm::cast<llvm::Function>(runtime_.headFunc.getCallee()));
  symbols_.insert("tail", llvm::cast<llvm::Function>(runtime_.tailFunc.getCallee()));
  symbols_.insert("length", llvm::cast<llvm::Function>(runtime_.lengthFunc.getCallee()));
}

// Module Compilation - Orchestration Layer

void CodeGen::compileModule(const Module& module) {
  for (const auto& decl : module.declarations) {
    compileDecl(*decl);
  }
}

void CodeGen::compileDecl(const Decl& decl) {
  // Delegate all declaration compilation to DeclCompiler
  std::visit(
      [this](const auto& node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, FunctionDecl>) {
          declCompiler_.genFunctionDecl(node);
        } else if constexpr (std::is_same_v<T, TypeDecl>) {
          declCompiler_.genTypeDecl(node);
        } else if constexpr (std::is_same_v<T, ModuleDecl>) {
          declCompiler_.genModuleDecl(node);
        } else if constexpr (std::is_same_v<T, ImportDecl>) {
          declCompiler_.genImportDecl(node);
        } else if constexpr (std::is_same_v<T, TraitDecl>) {
          declCompiler_.genTraitDecl(node);
        } else if constexpr (std::is_same_v<T, ImplDecl>) {
          declCompiler_.genImplDecl(node);
        }
      },
      decl.node);
}

// Declaration Compilation - Delegation to DeclCompiler

void CodeGen::genFunctionDecl(const FunctionDecl& funcDecl) {
  declCompiler_.genFunctionDecl(funcDecl);
}

void CodeGen::genTypeDecl(const TypeDecl& typeDecl) {
  declCompiler_.genTypeDecl(typeDecl);
}

void CodeGen::genModuleDecl(const ModuleDecl& moduleDecl) {
  declCompiler_.genModuleDecl(moduleDecl);
}

void CodeGen::genImportDecl(const ImportDecl& importDecl) {
  declCompiler_.genImportDecl(importDecl);
}

void CodeGen::genTraitDecl(const TraitDecl& traitDecl) {
  declCompiler_.genTraitDecl(traitDecl);
}

void CodeGen::genImplDecl(const ImplDecl& implDecl) {
  declCompiler_.genImplDecl(implDecl);
}

void CodeGen::genCurriedConstructor(const std::string& ctorName, size_t arity, int tag) {
  // DeclCompiler handles this internally during genTypeDecl
  (void)ctorName;
  (void)arity;
  (void)tag;
}

// Expression Generation - Delegation to ExprGenerator and ClosureConverter

llvm::Value* CodeGen::genExpr(const Expr& expr) {
  return exprGen_.genExpr(expr);
}

llvm::Value* CodeGen::genVar(const Var& var) {
  return exprGen_.genVar(var);
}

llvm::Value* CodeGen::genLit(const Lit& lit) {
  return exprGen_.genLit(lit);
}

llvm::Value* CodeGen::genBinOp(const BinOp& binOp) {
  return exprGen_.genBinOp(binOp);
}

llvm::Value* CodeGen::genIf(const If& ifExpr) {
  return exprGen_.genIf(ifExpr);
}

llvm::Value* CodeGen::genBlock(const Block& block) {
  return exprGen_.genBlock(block);
}

llvm::Value* CodeGen::genStrict(const Strict& strict) {
  return exprGen_.genStrict(strict);
}

llvm::Value* CodeGen::genBind(const Bind& bind) {
  return exprGen_.genBind(bind);
}

llvm::Value* CodeGen::genList(const List& list) {
  return exprGen_.genList(list);
}

llvm::Value* CodeGen::genRecord(const Record& record) {
  return exprGen_.genRecord(record);
}

llvm::Value* CodeGen::genRecordAccess(const RecordAccess& access) {
  return exprGen_.genRecordAccess(access);
}

llvm::Value* CodeGen::genRecordUpdate(const RecordUpdate& update) {
  return exprGen_.genRecordUpdate(update);
}

// Closure and Pattern Matching - Delegation to ClosureConverter

llvm::Value* CodeGen::genLambda(const Lambda& lambda, const std::string& recursiveName) {
  (void)recursiveName;  // Reserved for future named recursion support
  return closureConv_.genLambda(lambda);
}

llvm::Value* CodeGen::genApp(const App& app) {
  return closureConv_.genApp(app);
}

llvm::Value* CodeGen::genLet(const Let& let) {
  return closureConv_.genLet(let);
}

llvm::Value* CodeGen::genMatch(const Match& match) {
  return closureConv_.genMatch(match);
}

std::set<std::string> CodeGen::findFreeVariables(const Expr& expr,
                                                 const std::set<std::string>& boundVars) {
  // Internal helper - not exposed publicly
  (void)expr;
  (void)boundVars;
  return {};
}

llvm::Value* CodeGen::createClosure(llvm::Function* func,
                                    const std::vector<llvm::Value*>& envValues) {
  // Internal helper - handled by closureConv internally
  (void)func;
  (void)envValues;
  return nullptr;
}

llvm::Value* CodeGen::genLambdaImpl(const std::vector<PatternPtr>& params, const ExprPtr& body) {
  // Internal helper - use genLambda or genLambdaImplSlice instead
  (void)params;
  (void)body;
  return nullptr;
}

llvm::Value* CodeGen::genLambdaImplSlice(const std::vector<PatternPtr>& params,
                                         size_t startIdx,
                                         const ExprPtr& body) {
  // Public interface - delegate to ClosureConverter
  return closureConv_.genLambdaImplSlice(params, startIdx, body);
}

// Type Conversion - Delegation to TypeConverter

llvm::Type* CodeGen::toLLVMType(const InferTypePtr& type) {
  return typeConverter_.toLLVMType(type);
}

llvm::Type* CodeGen::getValueType() {
  return typeConverter_.getValueType();
}

llvm::Type* CodeGen::getThunkType() {
  return typeConverter_.getThunkType();
}

llvm::Type* CodeGen::getClosureType() {
  return typeConverter_.getClosureType();
}

llvm::Type* CodeGen::getConsType() {
  return typeConverter_.getConsType();
}

// IR Helpers - Delegation to IRHelpers

llvm::Value* CodeGen::boxValue(llvm::Value* value, llvm::Type* type) {
  return irHelpers_.boxValue(value, type);
}

llvm::Value* CodeGen::unboxValue(llvm::Value* boxed, llvm::Type* targetType) {
  return irHelpers_.unboxValue(boxed, targetType);
}

llvm::Constant* CodeGen::createStringConstant(const std::string& str) {
  return irHelpers_.createStringConstant(str);
}

void CodeGen::emitError(const std::string& message) {
  irHelpers_.emitError(message);
}

void CodeGen::emitWarning(const std::string& message) {
  irHelpers_.emitWarning(message);
}

// GC Support - Delegation to GCHelpers

llvm::Value* CodeGen::createGCAlloc(llvm::Value* size, ObjectTag tag) {
  return gcHelpers_.createGCAlloc(size, tag);
}

llvm::Value* CodeGen::createGCAllocAtomic(llvm::Value* size, ObjectTag tag) {
  return gcHelpers_.createGCAllocAtomic(size, tag);
}

void CodeGen::emitWriteBarrier(llvm::Value* obj, llvm::Value* field, llvm::Value* value) {
  gcHelpers_.emitWriteBarrier(obj, field, value);
}

// Module Verification and Output

bool CodeGen::verifyModule() {
  std::string errorMsg;
  llvm::raw_string_ostream errorStream(errorMsg);

  if (llvm::verifyModule(*module_, &errorStream)) {
    emitError("Module verification failed:\n" + errorMsg);
    return false;
  }

  if (debugMode_) {
    emitWarning("Module verified successfully");
  }

  return true;
}

void CodeGen::printModule() {
  module_->print(llvm::errs(), nullptr);
}

// Output Generation - Delegation to TargetBackend

void CodeGen::emitLLVM(const std::string& filename) {
  backend_.emitLLVM(module_.get(), filename);
}

void CodeGen::emitObject(const std::string& filename) {
  backend_.emitObject(module_.get(), filename);
}

void CodeGen::emitExecutable(const std::string& filename, int optLevel) {
  (void)optLevel;  // Reserved for optimization level control
  backend_.emitExecutable(module_.get(), filename);
}

// Optimizer Pipeline (kept in codegen for convenience)

void OptimizerPipeline::optimize(llvm::Module* module, int optLevel) {
  if (optLevel == 0)
    return;

  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  llvm::ModulePassManager MPM;
  if (optLevel == 1)
    MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O1);
  else if (optLevel == 2)
    MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
  else
    MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);

  MPM.run(*module, MAM);
}

void OptimizerPipeline::addStandardPasses(llvm::Module* module, int optLevel) {
  (void)module;
  (void)optLevel;
}

void OptimizerPipeline::addSolisSpecificPasses(llvm::Module* module) {
  // Solis-specific optimizations
  for (auto& F : *module) {
    if (F.getName().starts_with("lambda_")) {
      F.addFnAttr(llvm::Attribute::AlwaysInline);
    }

    if (!F.isDeclaration() && F.size() == 1 && F.begin()->size() <= 3) {
      F.addFnAttr(llvm::Attribute::InlineHint);
    }
  }
}

}  // namespace solis
