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
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <cstdint>
#include <memory>
#include <vector>

using namespace klee;
using namespace llvm;

Fuzzer::Fuzzer(Module *originalModule) : originalModule(originalModule) {}

llvm::Function *
Fuzzer::getRecordCoverage(LLVMContext &context,
                          std::unique_ptr<llvm::Module> &module) {
  return Function::Create(FunctionType::get(Type::getVoidTy(context), false),
                          Function::ExternalLinkage, "__record_coverage",
                          module.get());
}

void Fuzzer::createInstumentation(LLVMContext &context,
                                  std::unique_ptr<llvm::Module> &module,
                                  llvm::Function *recordCoverageFunc) {
  for (auto &function : *module) {
    for (auto &bb : function) {
      builder->SetInsertPoint(&bb, bb.begin());
      builder->CreateCall(recordCoverageFunc);
    }
  }
  std::error_code EC;
  llvm::raw_fd_ostream File(
      "/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3Debug/"
      "instrumentedoutput.ll",
      EC, llvm::sys::fs::OF_None);
  if (EC) {
    llvm::errs() << "Error opening file: " << EC.message() << "\n";
  } else {
    llvm::errs() << "File opened yay ^): " << "\n";
    module->print(File, nullptr);
  }
}

void Fuzzer::initializeEngine() {
  InitializeNativeTarget();
  InitializeNativeTargetAsmPrinter();
  InitializeNativeTargetAsmParser();
  std::string error;
  auto clonedModule = CloneModule(*originalModule);
  LLVMContext &context = clonedModule->getContext();
  builder = std::unique_ptr<IRBuilder<>>(new IRBuilder<>(context));

  llvm::Function *recordCoverageFunc = getRecordCoverage(context, clonedModule);
  createInstumentation(context, clonedModule, recordCoverageFunc);

  executionEngine = EngineBuilder(std::move(clonedModule))
                        .setErrorStr(&error)
                        .setEngineKind(EngineKind::JIT)
                        .create();

  if (!executionEngine) {
    errs() << "unable to make jit: " << error << "\n";
    abort();
  }

  if (executionEngine) {
    executionEngine->addGlobalMapping(recordCoverageFunc,
                                      (void *)(&__record_coverage));
    sys::DynamicLibrary::LoadLibraryPermanently(
        "/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3Debug/lib/"
        "libkleeFuzzerIntrinsics.so");
  }
}

void Fuzzer::fuzz() {
  initializeEngine();
  void *harness = sys::DynamicLibrary::SearchForAddressOfSymbol("klee_harness");
  uint64_t mainAddr = executionEngine->getFunctionAddress("main");
  FuzzInfo harness_fi(
      (void (*)(const uint8_t *, size_t, uint64_t, uint32_t))harness, mainAddr);

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

KTest *Fuzzer::kTestFromBytesArray(std::vector<uint8_t> bytes) {
  uint8_t *bytesPtr = bytes.data();
  size_t bytesSize = bytes.size();
  auto harness = (void (*)(const uint8_t *, size_t, uint64_t, uint32_t))
      sys::DynamicLibrary::SearchForAddressOfSymbol("klee_harness");
  uint64_t mainAddr = executionEngine->getFunctionAddress("main");
  (harness)(bytesPtr, bytesSize, mainAddr, 0);
  auto generator = (bool (*)(
      KTest *))sys::DynamicLibrary::SearchForAddressOfSymbol("generateKTest");
  KTest *ktest = (KTest *)calloc(1, sizeof(*ktest));
  bool success = (*generator)(ktest);
  if (!success) {
    return 0;
  }
  return ktest;
}