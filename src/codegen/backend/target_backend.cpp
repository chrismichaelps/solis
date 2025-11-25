// Solis Programming Language - Target Backend
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

#include "codegen/backend/target_backend.hpp"

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Triple.h>

#include <cstdlib>
#include <iostream>

namespace solis {

void TargetBackend::emitLLVM(llvm::Module* module, const std::string& filename) {
  std::error_code EC;
  llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

  if (EC) {
    diags_.emitError("Could not open file: " + EC.message());
    return;
  }

  module->print(dest, nullptr);
}

void TargetBackend::emitObject(llvm::Module* module, const std::string& filename) {
  // Get module's target triple
  std::string error;
  std::string targetTriple = getTargetTriple(module);

  const llvm::Target* target = llvm::TargetRegistry::lookupTarget(targetTriple, error);

  if (!target) {
    diags_.emitError("Failed to lookup target: " + error);
    return;
  }

  // Create target machine with PIC relocation model
  llvm::TargetOptions opt;
  llvm::TargetMachine* targetMachine =
      target->createTargetMachine(targetTriple, "generic", "", opt, llvm::Reloc::PIC_);

  module->setDataLayout(targetMachine->createDataLayout());

  // Open output file
  std::error_code EC;
  llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

  if (EC) {
    diags_.emitError("Could not open file: " + EC.message());
    delete targetMachine;
    return;
  }

  // Emit object code using legacy pass manager
  llvm::legacy::PassManager pass;
  if (targetMachine->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
    diags_.emitError("Target machine cannot emit object files");
    delete targetMachine;
    return;
  }

  pass.run(*module);
  dest.flush();

  delete targetMachine;
}

void TargetBackend::emitExecutable(llvm::Module* module, const std::string& filename) {
  // Emit object file to temporary location
  std::string objectFile = filename + ".o";
  emitObject(module, objectFile);

  // Link with system linker
  // Platform-specific: clang++ on macOS, g++ on Linux
  std::string linkCmd;

#ifdef __APPLE__
  linkCmd = "clang++ -o " + filename + " " + objectFile;
#else
  linkCmd = "g++ -o " + filename + " " + objectFile;
#endif

  // Add runtime library if available
  linkCmd += " src/runtime.o 2>/dev/null || true";

  int result = std::system(linkCmd.c_str());

  if (result != 0) {
    // Linking failed but object file remains
    std::cerr << "Warning: Linking failed, object file saved to: " << objectFile << std::endl;
  } else {
    // Clean up object file on successful linking
    std::remove(objectFile.c_str());
  }
}

std::string TargetBackend::getTargetTriple(llvm::Module* module) {
  std::string targetTriple = module->getTargetTriple().str();

  // Use host triple if not specified in module
  if (targetTriple.empty()) {
    targetTriple = LLVM_HOST_TRIPLE;
  }

  return targetTriple;
}

}  // namespace solis
