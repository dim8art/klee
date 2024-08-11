#ifndef KLEE_FUZZER_EXT_H
#define KLEE_FUZZER_EXT_H

extern "C" {

struct FuzzInfo {
  void (*harness)();
  FuzzInfo(void (*harness)()) : harness(harness) {}
};
void fuzzInternal(FuzzInfo fi);
}
#endif