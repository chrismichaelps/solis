// Solis Programming Language - Declaration Compilation Module
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/ir/decl_gen.hpp"

#include "codegen/codegen.hpp"
#include "codegen/ir/closure.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Verifier.h>

namespace solis {

DeclCompiler::DeclCompiler(llvm::LLVMContext& context,
                           llvm::Module& module,
                           llvm::IRBuilder<>& builder,
                           SymbolTable& symbols,
                           TypeConverter& types,
                           IRHelpers& helpers,
                           GCHelpers& gcHelpers,
                           RuntimeFunctions& runtime,
                           std::map<std::string, int>& constructorTags,
                           std::map<std::string, std::vector<TraitMethodInfo>>& traitRegistry,
                           std::map<std::string, std::vector<llvm::Function*>>& implVTables)
    : context_(context)
    , module_(module)
    , builder_(builder)
    , symbols_(symbols)
    , types_(types)
    , helpers_(helpers)
    , gcHelpers_(gcHelpers)
    , runtime_(runtime)
    , constructorTags_(constructorTags)
    , traitRegistry_(traitRegistry)
    , implVTables_(implVTables) {
  // Cache common types
  int1Type_ = llvm::Type::getInt1Ty(context_);
  int64Type_ = llvm::Type::getInt64Ty(context_);
  int8PtrType_ = llvm::PointerType::get(context_, 0);
}

void DeclCompiler::genFunctionDecl(const FunctionDecl& funcDecl) {
  // Function declaration compilation strategy:
  // 1. Zero-parameter functions: thunk wrappers for constants
  // 2. Single-parameter: direct function
  // 3. Multi-parameter: curried via closure converter
  //
  // All functions use uniform signature: void* fn(void* env, void* arg)
  // This enables first-class functions and higher-order programming

  if (!closureConv_) {
    helpers_.emitError("Closure converter not initialized");
    return;
  }

  // Determine function name (thunk prefix for constants)
  std::string funcName = funcDecl.name;
  if (funcDecl.params.empty()) {
    funcName = "thunk_" + funcName;
  }

  // Create function with uniform signature
  std::vector<llvm::Type*> paramTypes = {int8PtrType_, int8PtrType_};
  llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, paramTypes, false);

  llvm::Function* function =
      llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, funcName, &module_);

  // Add optimization hints
  function->addFnAttr(llvm::Attribute::NoUnwind);

  currentFunction_ = function;

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context_, "entry", function);
  builder_.SetInsertPoint(entry);

  symbols_.enterScope();

  // Bind first parameter (if any)
  llvm::Value* argArg = function->getArg(1);
  argArg->setName("arg");

  if (!funcDecl.params.empty()) {
    const PatternPtr& param = funcDecl.params[0];
    if (auto* varPat = std::get_if<VarPat>(&param->node)) {
      symbols_.insert(varPat->name, argArg);
    }
  }

  // Generate function body
  llvm::Value* retValue;
  if (funcDecl.params.size() > 1) {
    // Multi-parameter: curry remaining parameters via closure converter
    retValue = closureConv_->genLambdaImplSlice(funcDecl.params, 1, funcDecl.body);
  } else {
    // Zero or single parameter: generate body directly
    // Note: This requires exprGen to be set in closureConv
    // The body will use genExpr which is in exprGen
    retValue = closureConv_->genLambdaImplSlice(funcDecl.params,
                                                funcDecl.params.size(),
                                                funcDecl.body);
  }

  if (retValue) {
    // Auto-box return value for uniform calling convention
    if (retValue->getType()->isIntegerTy() && retValue->getType() != int1Type_) {
      retValue = helpers_.boxValue(retValue, retValue->getType());
    }
    builder_.CreateRet(retValue);
  } else {
    builder_.CreateRet(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)));
  }

  symbols_.exitScope();

  // Verify function correctness
  if (llvm::verifyFunction(*function, &llvm::errs())) {
    helpers_.emitError("Function verification failed: " + funcName);
  }
}

void DeclCompiler::genTypeDecl(const TypeDecl& typeDecl) {
  // ADT type declaration compilation
  // Generates constructors for each variant with proper tagging
  //
  // Constructor types:
  // - Nullary: Zero-argument, returns tagged constant
  // - Unary: Single argument, simple wrapper
  // - N-ary: Multi-argument, uses currying
  //
  // All constructors assigned unique tags for pattern matching
  // Tags stored in constructorTags_ map for Match compilation

  if (auto* adt = std::get_if<std::vector<std::pair<std::string, std::vector<TypePtr>>>>(
          &typeDecl.rhs)) {
    int tag = 0;
    for (const auto& [ctorName, args] : *adt) {
      if (args.empty()) {
        // Nullary constructor: constant ADT value
        // Layout: [tag (i64)]
        // Example: data Bool = True | False
        //          True :: Bool (no arguments)
        llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, {}, false);
        llvm::Function* function =
            llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, ctorName, &module_);

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context_, "entry", function);
        builder_.SetInsertPoint(entry);

        // Allocate ADT object with tag only
        llvm::Value* size = llvm::ConstantInt::get(int64Type_, 8);
        llvm::Value* mem = gcHelpers_.createGCAlloc(size, ObjectTag::ADT);

        // Store constructor tag
        llvm::Value* tagVal = llvm::ConstantInt::get(int64Type_, tag);
        llvm::Value* tagPtr = builder_.CreatePointerCast(mem,
                                                         llvm::PointerType::get(int64Type_, 0));
        builder_.CreateStore(tagVal, tagPtr);

        builder_.CreateRet(mem);

        symbols_.insert(ctorName, function);
        constructorTags_[ctorName] = tag;
      } else if (args.size() == 1) {
        // Unary constructor: single-argument wrapper
        // Layout: [tag (i64), value (void*)]
        // Example: data Maybe a = Just a | Nothing
        //          Just :: a -> Maybe a
        std::vector<llvm::Type*> paramTypes = {int8PtrType_, int8PtrType_};
        llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, paramTypes, false);
        llvm::Function* function =
            llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, ctorName, &module_);
        function->addFnAttr(llvm::Attribute::NoUnwind);

        llvm::BasicBlock* entry = llvm::BasicBlock::Create(context_, "entry", function);
        builder_.SetInsertPoint(entry);

        // Allocate ADT object: [Tag, Field]
        llvm::Value* size = llvm::ConstantInt::get(int64Type_, 16);
        llvm::Value* mem = gcHelpers_.createGCAlloc(size, ObjectTag::ADT);

        // Store tag at offset 0
        llvm::Value* tagVal = llvm::ConstantInt::get(int64Type_, tag);
        llvm::Value* tagPtr = builder_.CreatePointerCast(mem,
                                                         llvm::PointerType::get(int64Type_, 0));
        builder_.CreateStore(tagVal, tagPtr);

        // Store argument at offset 8
        llvm::Value* arg = function->getArg(1);
        llvm::Value* fieldPtr = builder_.CreateGEP(int8PtrType_,
                                                   mem,
                                                   llvm::ConstantInt::get(int64Type_, 8));
        builder_.CreateStore(arg,
                             builder_.CreatePointerCast(fieldPtr,
                                                        llvm::PointerType::get(int8PtrType_, 0)));

        builder_.CreateRet(mem);

        symbols_.insert(ctorName, function);
        constructorTags_[ctorName] = tag;
      } else {
        // Multi-argument constructor: curried application
        // Layout: [tag (i64), field1 (void*), field2 (void*), ...]
        // Example: data Pair a b = Pair a b
        //          Pair :: a -> b -> Pair a b
        //
        // Implementation generates chain of closures:
        // Pair = \a -> \b -> ADT{tag=0, fields=[a,b]}
        genCurriedConstructor(ctorName, args.size(), tag);
        constructorTags_[ctorName] = tag;
      }

      tag++;
    }
  }
}

void DeclCompiler::genCurriedConstructor(const std::string& ctorName, size_t arity, int tag) {
  // Curried constructor generation for multi-argument ADTs
  // Transforms Ctor :: a -> b -> c -> ADT into nested closures
  //
  // Strategy:
  // 1. First function takes arg1, returns closure capturing arg1
  // 2. Inner function takes arg2, constructs final ADT
  // 3. For arity > 2, would need additional nesting (not yet implemented)
  //
  // Design rationale:
  // - Enables partial application: (Pair 1) returns closure waiting for second arg
  // - Uniform calling convention maintained throughout
  // - Pattern matching works correctly with tag checking

  // Generate first constructor function
  std::vector<llvm::Type*> paramTypes = {int8PtrType_, int8PtrType_};
  llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, paramTypes, false);

  llvm::Function* firstFunc =
      llvm::Function::Create(funcType, llvm::Function::ExternalLinkage, ctorName, &module_);
  firstFunc->addFnAttr(llvm::Attribute::NoUnwind);

  llvm::BasicBlock* entry = llvm::BasicBlock::Create(context_, "entry", firstFunc);
  builder_.SetInsertPoint(entry);

  llvm::Value* arg1 = firstFunc->getArg(1);
  arg1->setName("arg1");

  if (arity == 2) {
    // Two-argument constructor: most common case
    // Generate inner function for second argument
    static int ctorCount = 0;
    std::string innerName = ctorName + "_inner_" + std::to_string(ctorCount++);

    llvm::Function* innerFunc =
        llvm::Function::Create(funcType, llvm::Function::InternalLinkage, innerName, &module_);
    innerFunc->addFnAttr(llvm::Attribute::NoUnwind);
    innerFunc->addFnAttr(llvm::Attribute::AlwaysInline);

    llvm::BasicBlock* innerEntry = llvm::BasicBlock::Create(context_, "entry", innerFunc);
    llvm::BasicBlock* savedBlock = builder_.GetInsertBlock();
    builder_.SetInsertPoint(innerEntry);

    // Extract first argument from environment
    llvm::Value* envArg = innerFunc->getArg(0);
    llvm::Value* arg2 = innerFunc->getArg(1);
    arg2->setName("arg2");

    // Allocate ADT object: [Tag, Arg1, Arg2]
    llvm::Value* size = llvm::ConstantInt::get(int64Type_, 24);
    llvm::Value* mem = gcHelpers_.createGCAlloc(size, ObjectTag::ADT);

    // Store tag
    llvm::Value* tagVal = llvm::ConstantInt::get(int64Type_, tag);
    llvm::Value* tagPtr = builder_.CreatePointerCast(mem, llvm::PointerType::get(int64Type_, 0));
    builder_.CreateStore(tagVal, tagPtr);

    // Store first argument (from environment)
    llvm::Value* field1Ptr = builder_.CreateGEP(int8PtrType_,
                                                mem,
                                                llvm::ConstantInt::get(int64Type_, 8));
    builder_.CreateStore(envArg,
                         builder_.CreatePointerCast(field1Ptr,
                                                    llvm::PointerType::get(int8PtrType_, 0)));

    // Store second argument
    llvm::Value* field2Ptr = builder_.CreateGEP(int8PtrType_,
                                                mem,
                                                llvm::ConstantInt::get(int64Type_, 16));
    builder_.CreateStore(arg2,
                         builder_.CreatePointerCast(field2Ptr,
                                                    llvm::PointerType::get(int8PtrType_, 0)));

    builder_.CreateRet(mem);

    // Return to first function and create closure
    builder_.SetInsertPoint(savedBlock);

    if (!closureConv_) {
      helpers_.emitError("Closure converter not initialized");
      builder_.CreateRet(
          llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)));
    } else {
      llvm::Value* closure = closureConv_->createClosure(innerFunc, {arg1});
      builder_.CreateRet(closure);
    }
  } else {
    // N-argument constructors (N > 2) require recursive nesting
    // Design decision: Limit to 2 arguments, use nested types for more
    // Rationale: Keeps implementation simple, encourages better ADT design
    helpers_.emitError("Constructors with more than 2 arguments not yet supported - use "
                       "nested types");
    builder_.CreateRet(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(int8PtrType_)));
  }

  symbols_.insert(ctorName, firstFunc);
}

void DeclCompiler::genModuleDecl(const ModuleDecl& moduleDecl) {
  // Module declaration records module name in IR metadata
  // Enables debugging and improves error messages
  // No runtime code generated
  if (!moduleDecl.name.empty()) {
    module_.setModuleIdentifier(moduleDecl.name);
  }
}

void DeclCompiler::genImportDecl(const ImportDecl& importDecl) {
  // Import declarations handled by module resolver at compile time
  // Functions from imported modules already available via symbol resolution
  // No IR generation needed - imports are link-time concern
  (void)importDecl;
}

void DeclCompiler::genTraitDecl(const TraitDecl& traitDecl) {
  // Trait declaration compilation for type class system
  // Stores method signatures in registry for vtable construction
  //
  // Design:
  // - Traits define interface (method names and types)
  // - Implementations provide concrete methods
  // - Vtables enable dynamic dispatch for trait-constrained functions
  //
  // Registry format: trait_name -> [method_info...]
  // Each method_info contains name and LLVM function type

  std::vector<TraitMethodInfo> methods;

  for (const auto& [methodName, methodType] : traitDecl.methods) {
    // Convert AST type to LLVM function type
    // Default signature for uniform calling: void* fn(void* self, void* arg)
    // Actual signature would require type inference integration
    std::vector<llvm::Type*> paramTypes = {int8PtrType_, int8PtrType_};
    llvm::FunctionType* funcType = llvm::FunctionType::get(int8PtrType_, paramTypes, false);

    methods.push_back(TraitMethodInfo{methodName, funcType});
  }

  traitRegistry_[traitDecl.name] = methods;
}

void DeclCompiler::genImplDecl(const ImplDecl& implDecl) {
  // Implementation declaration compilation
  // Generates vtable for trait-based dynamic dispatch
  //
  // Vtable structure: Array of function pointers for trait methods
  // Stored as global constant with internal linkage
  //
  // Method naming: TraitName_TypeName_MethodName (mangling)
  // Enables multiple implementations of same trait for different types
  //
  // Design rationale:
  // - Vtables enable runtime polymorphism without boxing overhead
  // - Mangled names avoid conflicts between implementations
  // - Global constants allow efficient dispatch (no allocation)

  std::vector<llvm::Function*> vtable;
  std::string traitName = implDecl.traitName.value_or("Trait");

  // Extract type name from TypePtr
  std::string typeName = "UnknownType";
  if (implDecl.type) {
    if (auto* tyCon = std::get_if<TyCon>(&implDecl.type->node)) {
      typeName = tyCon->name;
    } else if (auto* tyVar = std::get_if<TyVar>(&implDecl.type->node)) {
      typeName = tyVar->name;
    }
  }

  std::string implKey = traitName + "_" + typeName;

  // Compile each trait method implementation
  for (const FunctionDecl& method : implDecl.methods) {
    // Generate method with mangled name for type-specific dispatch
    // Original name preserved in symbol table under trait context
    std::string mangledName = traitName + "_" + typeName + "_" + method.name;

    // Temporarily rename method for compilation
    std::string originalName = method.name;
    const_cast<FunctionDecl&>(method).name = mangledName;

    genFunctionDecl(method);

    // Retrieve generated function for vtable
    llvm::Function* implFunc = module_.getFunction(mangledName);
    if (implFunc) {
      vtable.push_back(implFunc);
    }

    // Restore original name
    const_cast<FunctionDecl&>(method).name = originalName;
  }

  // Store vtable for runtime dispatch
  implVTables_[implKey] = vtable;

  // Generate vtable global constant if methods exist
  if (!vtable.empty()) {
    std::vector<llvm::Constant*> vtableEntries;
    for (auto* func : vtable) {
      vtableEntries.push_back(llvm::cast<llvm::Constant>(func));
    }

    // Create vtable array type
    llvm::ArrayType* vtableType = llvm::ArrayType::get(llvm::PointerType::get(context_, 0),
                                                       vtable.size());

    // Create vtable constant
    llvm::Constant* vtableArray = llvm::ConstantArray::get(vtableType, vtableEntries);

    // Create global vtable variable with internal linkage
    // Internal linkage: not exported, compiler can optimize
    new llvm::GlobalVariable(module_,
                             vtableType,
                             true,
                             llvm::GlobalValue::InternalLinkage,
                             vtableArray,
                             "vtable_" + implKey);
  }
}

}  // namespace solis
