#ifndef KLEE_FUZZER_H
#define KLEE_FUZZER_H

#include <vector>
#include <cstdint>

namespace llvm {
class ExecutionEngine;
class Module;
} // namespace llvm

extern "C" {
struct KTest;
struct KTestObject;
}

namespace klee {

class Fuzzer {
  llvm::ExecutionEngine *executionEngine;
  llvm::Module *m;

public:
  Fuzzer(llvm::Module *m) : m(m) {}

  void initializeEngine();

  void fuzz();

  std::vector<uint8_t> bytesArrayFromKtest(KTest * kTest);
};

} // namespace klee

#endif /* KLEE_FUZZER_H */
