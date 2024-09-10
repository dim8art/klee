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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/time.h>
#include <vector>

#include "klee/klee.h"

#include "klee/ADT/KTest.h"

const uint8_t *bytes;
static size_t numBytes = 0;
static size_t bytesPosition = 0;
struct VarInfo {
  uint8_t *bytes;
  size_t numBytes;
  char *name = nullptr;

  VarInfo(uint8_t *_bytes, size_t _numBytes, const char *_name)
      : numBytes(_numBytes) {
    bytes = (unsigned char *)calloc(numBytes, sizeof(*bytes));
    std::memcpy(bytes, _bytes, numBytes);
    name = (char *)calloc(std::strlen(_name) + 1, sizeof(name));
    std::strcpy(name, _name);
  }

  VarInfo(const VarInfo &vi) : numBytes(vi.numBytes) {
    bytes = (unsigned char *)calloc(numBytes, sizeof(*bytes));
    std::memcpy(bytes, vi.bytes, numBytes);
    name = (char *)calloc(std::strlen(vi.name) + 1, sizeof(name));
    std::strcpy(name, vi.name);
  }

  ~VarInfo() {
    free(bytes);
    free(name);
  }
};

static std::vector<VarInfo> varEntries;

static bool generateKTest(KTest *res) {
  res->numObjects = varEntries.size();
  res->objects = (KTestObject *)calloc(res->numObjects, sizeof(*res->objects));
  for (unsigned i = 0; i < varEntries.size(); i++) {
    VarInfo &vi = varEntries[i];
    KTestObject *o = &res->objects[i];
    o->name = (char *)calloc(std::strlen(vi.name) + 1, sizeof(*o->name));
    std::strcpy(o->name, vi.name);
    o->address = (uint64_t)(vi.bytes);
    o->numBytes = vi.numBytes;
    o->bytes = (unsigned char *)calloc(o->numBytes, sizeof(*o->bytes));
    std::memcpy(o->bytes, vi.bytes, o->numBytes);
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
  fprintf(stdout, "using klee_make_symbol on var %s\n", name);
  uint8_t *value = (uint8_t *)array;
  for (size_t i = bytesPosition; i < bytesPosition + nbytes; i++) {
    value[i - bytesPosition] = bytes[i];
  }
  bytesPosition += nbytes;
  varEntries.push_back(VarInfo(value, nbytes, name));
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

void klee_harness(const uint8_t *_bytes, size_t _numBytes, uint64_t mainAddr,
                  uint32_t id) {
  varEntries.clear();
  printf("klee_harness began\n");
  bytes = _bytes;
  numBytes = _numBytes;
  bytesPosition = 0;
  void (*f)() = (void (*)())mainAddr;
  printf("%u, %u , %u, %u\n", *_bytes, _numBytes, mainAddr, id);
  (*f)();
  KTest *ktest = (KTest *)calloc(1, sizeof(*ktest));

  generateKTest(ktest);
  kTest_toFile(
      ktest,
      ("/home/dim8art/klee_build/klee_build140bitwuzla_stp_z3Debug/testktest" +
       std::to_string(id))
          .c_str());
  fprintf(stdout, "ktest generated 1 with address %s\n",
          (std::to_string(id)).c_str());
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
