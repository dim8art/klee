//===-- fuzzer_intrinsics.cpp
//------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//


#include <cassert>
#include <vector>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/time.h>

#include "klee/klee.h"

#include "klee/ADT/KTest.h"


const uint8_t *bytes;
static size_t numBytes = 0;
static size_t bytesPosition = 0;

struct VarInfo {
  void *array;
  size_t nbytes;
  char *name = nullptr;
  VarInfo(void *_array, size_t _nbytes, const char *_name) : array(_array), nbytes(_nbytes){
    name = (char *)calloc(std::strlen(_name) + 1, sizeof(name));
    std::strcpy(name, _name);
  }
};

static std::vector<VarInfo> varEntries;

static bool generateKTest(KTest *res){
  res = (KTest *)calloc(1, sizeof(*res));
  res->numObjects = varEntries.size();
  res->objects = (KTestObject *)calloc(res->numObjects, sizeof(*res->objects));
  for (unsigned i = 0; i < varEntries.size(); i++) {
    VarInfo &vi = varEntries[i];
    KTestObject *o = &res->objects[i];
    o->name = (char *)calloc(std::strlen(vi.name) + 1, sizeof(*o->name));
    std::strcpy(o->name, vi.name);
    o->address = (uint64_t)(vi.array);
    o->numBytes = vi.nbytes;
    o->bytes = (unsigned char *)calloc(o->numBytes, sizeof(*o->bytes));
    uint8_t *value = static_cast<uint8_t *>(vi.array);
    for (size_t j = 0; j < o->numBytes; j++) {
      o->bytes[j] = value[j];
    }
    o->numPointers = 0;
    o->pointers = nullptr;
  }
  return true;
}

static unsigned char rand_byte(void) {
  unsigned x = rand();
  x ^= x >> 16;
  x ^= x >> 8;
  return x & 0xFF;
}

static void report_internal_error(const char *msg, ...)
    __attribute__((format(printf, 1, 2)));
static void report_internal_error(const char *msg, ...) {
  fprintf(stderr, "KLEE_RUN_TEST_ERROR: ");
  va_list ap;
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);
  fprintf(stderr, "\n");
  char *testErrorsNonFatal = getenv("KLEE_RUN_TEST_ERRORS_NON_FATAL");
  if (testErrorsNonFatal && !strcmp(testErrorsNonFatal, "STOP")) {
    fprintf(stderr, "KLEE_RUN_TEST_ERROR: Stop execution without an error\n");
    exit(0);
  } else if (testErrorsNonFatal && !strcmp(testErrorsNonFatal, "FORCE")) {
    fprintf(stderr, "KLEE_RUN_TEST_ERROR: Forcing execution to continue\n");
  } else {
    exit(1);
  }
}

static void klee_make_symbol(void *array, size_t nbytes, const char *name) {
  fprintf(stdout, "using klee_make_symbol on var %s with value\n", name);
  uint8_t *value = (uint8_t *)array;
  for (size_t i = bytesPosition; i < bytesPosition + nbytes; i++) {
    value[i-bytesPosition] = bytes[i];
  }
  bytesPosition += nbytes;
  varEntries.push_back(VarInfo(array, nbytes, name));
}

void klee_make_symbolic(void *array, size_t nbytes, const char *name) {
  klee_make_symbol(array, nbytes, name);
}

void klee_make_mock(void *ret_array, size_t ret_nbytes, const char *fname) {
  klee_make_symbol(ret_array, ret_nbytes, fname);
}

void klee_silent_exit(int x) { exit(x); }

uintptr_t klee_choose(uintptr_t n) {
  uintptr_t x;
  klee_make_symbolic(&x, sizeof x, "klee_choose");
  if (x >= n)
    report_internal_error("klee_choose failure. max = %ld, got = %ld\n", n, x);
  return x;
}

unsigned klee_is_replay() { return 1; }

void klee_assume(uintptr_t x) {
  fprintf(stdout, "using klee_assume on expression %zd\n", x);
}

void klee_harness(const uint8_t *_bytes, size_t _numBytes, uint64_t mainAddr) {
  printf("klee_harness began\n");
  bytes = _bytes;
  numBytes = _numBytes;
    printf("%ul\n", numBytes);
  bytesPosition = 0;
  void (*f)() = (void (*)())mainAddr;
  (*f)();
}


#define KLEE_GET_VALUE_STUB(suffix, type)                                      \
  type klee_get_value##suffix(type x) { return x; }

KLEE_GET_VALUE_STUB(f, float)
KLEE_GET_VALUE_STUB(d, double)
KLEE_GET_VALUE_STUB(l, long)
KLEE_GET_VALUE_STUB(ll, long long)
KLEE_GET_VALUE_STUB(_i32, int32_t)
KLEE_GET_VALUE_STUB(_i64, int64_t)

#undef KLEE_GET_VALUE_STUB

int klee_range(int begin, int end, const char *name) {
  int x;
  klee_make_symbolic(&x, sizeof x, name);
  if (x < begin || x >= end) {
    report_internal_error("invalid klee_range(%u,%u,%s) value, got: %u\n",
                          begin, end, name, x);
  }
  return x;
}

void klee_prefer_cex(__attribute__((unused)) void *object,
                     __attribute__((unused)) uintptr_t condition) {}

void klee_abort() { abort(); }

void klee_print_expr(__attribute__((unused)) const char *msg, ...) {}

void klee_set_forking(__attribute__((unused)) unsigned enable) {}
