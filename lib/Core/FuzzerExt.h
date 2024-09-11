#ifndef KLEE_FUZZER_EXT_H
#define KLEE_FUZZER_EXT_H
#include <cstdint>
#include <cstdlib>
extern "C" {

struct FuzzInfo {
  void (*harness)(const uint8_t *, size_t, uint64_t, uint32_t);
  uint64_t mainAddr;
  FuzzInfo(void (*harness)(const uint8_t *, size_t, uint64_t, uint32_t),
           uint64_t mainAddr)
      : harness(harness), mainAddr(mainAddr) {}
};
struct FuzzSolution {
  uint8_t success;
  size_t numBytes;
  const uint8_t * bytes;
};

FuzzSolution getFuzzSolution();
void __record_coverage();
void fuzzInternal(FuzzInfo fi);

}
#endif