#ifndef KLEE_FUZZER_EXT_H
#define KLEE_FUZZER_EXT_H
#include <cstdint>
#include <cstdlib>
extern "C" {

struct FuzzInfo {
  void (*harness)(const int8_t *, size_t);
  FuzzInfo(void (*harness)(const int8_t *, size_t)) : harness(harness) {}
};
void fuzzInternal(FuzzInfo fi);
}
#endif