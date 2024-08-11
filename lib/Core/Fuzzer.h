#ifndef KLEE_FUZZER_H
#define KLEE_FUZZER_H

  namespace llvm {
  class ExecutionEngine;
  class Module;
  }

namespace klee {

class Fuzzer {
  llvm::ExecutionEngine *executionEngine;
  llvm::Module *m;

public:
  Fuzzer(llvm::Module *m) : m(m) {}

  void initializeEngine();

  void fuzz();
};

} // namespace klee

#endif /* KLEE_FUZZER_H */
