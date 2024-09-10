#ifndef KLEE_FUZZER_H
#define KLEE_FUZZER_H

#include "llvm/IR/IRBuilder.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {
class ExecutionEngine;
class Module;
class BasicBlock;
class LLVMContext;
} // namespace llvm

extern "C" {
struct KTest;
struct KTestObject;
}

namespace klee {

class Fuzzer {
private:
  llvm::ExecutionEngine *executionEngine;
  llvm::Module *originalModule;

  std::unique_ptr<llvm::IRBuilder<>> builder;

private:
  void initializeEngine();

  llvm::Function *
  addInstrumentationDeclaration(llvm::LLVMContext &context,
                                std::unique_ptr<llvm::Module> &module);
  void createInstumentation(llvm::LLVMContext &context,
                            std::unique_ptr<llvm::Module> &module);
  llvm::BasicBlock *cloneBasicBlock(llvm::BasicBlock *BB);

  std::vector<uint8_t> bytesArrayFromKtest(KTest *kTest);

public:
  Fuzzer(llvm::Module *m);
  void fuzz();
};

} // namespace klee

#endif /* KLEE_FUZZER_H */
