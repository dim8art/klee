//===-- fuzzer_intrinsics.c
//------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

/* Straight C for linking simplicity */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "klee/klee.h"

#include "klee/ADT/KTest.h"

const int8_t *bytes;
size_t numBytes = 0;
size_t bytesPosition = 0;
static KTest *testData = 0;
static unsigned testPosition = 0;
static uintptr_t *addresses;

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

void recursively_allocate(KTestObject *obj, size_t index, void *addr,
                          int lazy) {
  if (!lazy) {
    memcpy(addr, obj->bytes, obj->numBytes);
    addresses[index] = (uintptr_t)addr;
  } else {
    void *address = malloc(obj->numBytes);
    memcpy(address, obj->bytes, obj->numBytes);
    addresses[index] = (uintptr_t)address;
  }
  for (size_t i = 0; i < obj->numPointers; i++) {
    if (!addresses[obj->pointers[i].index]) {
      recursively_allocate(&testData->objects[obj->pointers[i].index],
                           obj->pointers[i].index, 0, 1);
    }
    void *offset_addr = (void *)(addresses[index] + (obj->pointers[i].offset));
    void *pointee_addr = (void *)(addresses[obj->pointers[i].index]);
    pointee_addr += obj->pointers[i].indexOffset;
    memcpy(offset_addr, &pointee_addr, sizeof(void *));
  }
  return;
}

static void klee_make_symbol(void *array, size_t nbytes, const char *name) {
  int8_t *value = array;
  for (int64_t i = bytesPosition; i < bytesPosition + nbytes; i++) {
    value[i] = bytes[i];
  }
  bytesPosition += nbytes;
  fprintf(stdout, "using klee_make_symbol on var %s\n", name);
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

void klee_harness(const int8_t *_bytes, size_t _numBytes) {
  bytes = _bytes;
  numBytes = _numBytes;
  bytesPosition = 0;
  main();
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
