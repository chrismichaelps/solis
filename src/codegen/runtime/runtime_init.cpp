// Solis Programming Language - Runtime Function Initialization
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/runtime/runtime_init.hpp"

#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>

namespace solis {

void RuntimeFunctions::initialize(llvm::Module* module, llvm::LLVMContext& context) {
  // Common types used across runtime functions
  llvm::Type* voidType = llvm::Type::getVoidTy(context);
  llvm::Type* int1Type = llvm::Type::getInt1Ty(context);
  llvm::Type* int8Type = llvm::Type::getInt8Ty(context);
  llvm::Type* int64Type = llvm::Type::getInt64Ty(context);
  llvm::Type* int8PtrType = llvm::PointerType::get(context, 0);

  // void* solis_alloc(size_t size, uint8_t tag)
  // Allocates heap object with GC header containing type tag
  llvm::FunctionType* allocType = llvm::FunctionType::get(int8PtrType,
                                                          {int64Type, int8Type},
                                                          false);
  allocFunc = module->getOrInsertFunction("solis_alloc", allocType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(allocFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addRetAttr(llvm::Attribute::NoAlias);  // Returns fresh memory
  }

  // void* solis_alloc_atomic(size_t size, uint8_t tag)
  // Allocates atomic (no pointers) object for primitives
  allocAtomicFunc = module->getOrInsertFunction("solis_alloc_atomic", allocType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(allocAtomicFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addRetAttr(llvm::Attribute::NoAlias);
  }

  // void solis_gc_write_barrier(void* obj, void* field, void* value)
  // Write barrier for generational/incremental GC
  // Called before storing pointer into heap object
  llvm::FunctionType* writeBarrierType =
      llvm::FunctionType::get(voidType, {int8PtrType, int8PtrType, int8PtrType}, false);
  gcWriteBarrierFunc = module->getOrInsertFunction("solis_gc_write_barrier", writeBarrierType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(gcWriteBarrierFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
  }

  // void* solis_create_thunk(void* (*fn)(void*), void* env)
  llvm::FunctionType* thunkFnType = llvm::FunctionType::get(int8PtrType, {int8PtrType}, false);
  llvm::PointerType* thunkFnPtrType = llvm::PointerType::get(thunkFnType, 0);
  llvm::FunctionType* createThunkType = llvm::FunctionType::get(int8PtrType,
                                                                {thunkFnPtrType, int8PtrType},
                                                                false);
  createThunkFunc = module->getOrInsertFunction("solis_create_thunk", createThunkType);

  // void* solis_force(void* thunk)
  llvm::FunctionType* forceType = llvm::FunctionType::get(int8PtrType, {int8PtrType}, false);
  forceThunkFunc = module->getOrInsertFunction("solis_force", forceType);

  // char* solis_string_concat(char* s1, char* s2)
  llvm::FunctionType* stringConcatType = llvm::FunctionType::get(int8PtrType,
                                                                 {int8PtrType, int8PtrType},
                                                                 false);
  stringConcatFunc = module->getOrInsertFunction("solis_string_concat", stringConcatType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(stringConcatFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addParamAttr(0, llvm::Attribute::ReadOnly);
    F->addParamAttr(1, llvm::Attribute::ReadOnly);
  }

  // int solis_string_eq(char* s1, char* s2)
  // String equality is pure: readonly args, no side effects
  llvm::FunctionType* stringEqType = llvm::FunctionType::get(int1Type,
                                                             {int8PtrType, int8PtrType},
                                                             false);
  stringEqFunc = module->getOrInsertFunction("solis_string_eq", stringEqType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(stringEqFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
    F->addParamAttr(0, llvm::Attribute::ReadOnly);
    F->addParamAttr(1, llvm::Attribute::ReadOnly);
  }

  // void* solis_print(char* str)
  // Print only reads memory, doesn't throw
  llvm::FunctionType* printType = llvm::FunctionType::get(int8PtrType, {int8PtrType}, false);
  printFunc = module->getOrInsertFunction("solis_print", printType);
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(printFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addParamAttr(0, llvm::Attribute::ReadOnly);
  }

  // char* solis_read_line()
  llvm::FunctionType* readLineType = llvm::FunctionType::get(int8PtrType, {}, false);
  readLineFunc = module->getOrInsertFunction("solis_read_line", readLineType);

  // void* solis_cons(void* head, void* tail)
  consFunc = module->getOrInsertFunction(
      "solis_cons", llvm::FunctionType::get(int8PtrType, {int8PtrType, int8PtrType}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(consFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
  }

  // solis_head: ptr -> ptr (pure accessor)
  headFunc = module->getOrInsertFunction(
      "solis_head", llvm::FunctionType::get(int8PtrType, {int8PtrType}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(headFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
  }

  // solis_tail: ptr -> ptr (pure accessor)
  tailFunc = module->getOrInsertFunction(
      "solis_tail", llvm::FunctionType::get(int8PtrType, {int8PtrType}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(tailFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
  }

  // solis_length: ptr -> i64 (pure length computation)
  lengthFunc = module->getOrInsertFunction(
      "solis_list_length", llvm::FunctionType::get(int64Type, {int8PtrType}, false));
  if (llvm::Function* F = llvm::dyn_cast<llvm::Function>(lengthFunc.getCallee())) {
    F->addFnAttr(llvm::Attribute::NoUnwind);
    F->addFnAttr(llvm::Attribute::ReadNone);
  }
}

void RuntimeFunctions::addOptimizationAttributes(llvm::Function* func,
                                                 bool noUnwind,
                                                 bool readNone,
                                                 bool readOnly) {
  if (noUnwind) {
    func->addFnAttr(llvm::Attribute::NoUnwind);
  }
  if (readNone) {
    func->addFnAttr(llvm::Attribute::ReadNone);
  }
  if (readOnly) {
    func->addFnAttr(llvm::Attribute::ReadOnly);
  }
}

}  // namespace solis
