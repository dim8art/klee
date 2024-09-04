#include "Fuzzer.h"
#include "FuzzerExt.h"
#include "klee/ADT/KTest.h"

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
#include "llvm/Transforms/Utils/Cloning.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace klee;

Fuzzer::Fuzzer(llvm::Module *m) : m(m) {}

void Fuzzer::initializeEngine() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();
  std::string error;
  auto mClone = llvm::CloneModule(*m);
  executionEngine = llvm::EngineBuilder(std::move(mClone))
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
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(
        "/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3Debug/lib/"
        "libkleeFuzzerIntrinsics.so"); // fix address
    // InitializeNativeTarget();
    // InitializeNativeTargetAsmPrinter();
    // InitializeNativeTargetAsmParser();
  }
}

void Fuzzer::fuzz() {
  initializeEngine();
  void *harness =
      llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("klee_harness");
  uint64_t mainAddr = executionEngine->getFunctionAddress("main");
  FuzzInfo harness_fi((void (*)(const uint8_t *, size_t, uint64_t, size_t))harness,
                      mainAddr);
  llvm::errs()<<(uint64_t)harness_fi.harness;
  fuzzInternal(harness_fi);
  
}

std::vector<uint8_t> Fuzzer::bytesArrayFromKtest(KTest *kTest) {
  if (!kTest) {
    return {};
  }
  std::vector<uint8_t> res;
  for (unsigned i = 0; i < kTest->numObjects; i++) {
    for (unsigned j = 0; j < kTest->objects[i].numBytes; j++) {
      res.push_back(kTest->objects[i].bytes[j]);
    }
  }
  return res;
}