#include "Fuzzer.h"
#include "FuzzerExt.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/MCJIT.h"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdint>
#include <memory>
#include <vector>

using namespace klee;

void Fuzzer::initializeEngine() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
  std::string error;
  auto mUniq = std::unique_ptr<llvm::Module>(m);
  executionEngine = llvm::EngineBuilder(std::move(mUniq))
                        .setErrorStr(&error)
                        .setEngineKind(llvm::EngineKind::JIT)
                        .create();

  if (!executionEngine) {
    llvm::errs() << "unable to make jit: " << error << "\n";
    abort();
  }

  // from ExecutionEngine::create
  if (executionEngine) {
    // Make sure we can resolve symbols in the program as well. The zero arg
    // to the function tells DynamicLibrary to load the program, not a
    // library.
    llvm::sys::DynamicLibrary::LoadLibraryPermanently("/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3Debug/lib/libkleeFuzzerIntrinsics.so"); //fix address
    // InitializeNativeTarget();
    // InitializeNativeTargetAsmPrinter();
    // InitializeNativeTargetAsmParser();
  }
}

void Fuzzer::fuzz() {
  initializeEngine();
  uint64_t f = executionEngine->getFunctionAddress("main");
  FuzzInfo fi((void (*)())f);
  fuzzInternal(fi);
}