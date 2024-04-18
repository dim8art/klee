//===-- SeedInfo.h ----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SEEDINFO_H
#define KLEE_SEEDINFO_H

#include "klee/Expr/Assignment.h"

#include <set>

extern "C" {
struct KTest;
struct KTestObject;
}

namespace klee {
class ExecutionState;
class TimingSolver;
class MemoryObject;


class SeedInfo {
public:
  Assignment assignment;
  std::shared_ptr<KTest> input;
  unsigned maxInstructions = 0;
  bool isCompleted = 0;
  unsigned inputPosition = 0;
  std::set<struct KTestObject *> used;
  std::string path;

public:
  static void KTestDeleter(KTest *kTest);

  ~SeedInfo(){}
  

  explicit SeedInfo(KTest *input, unsigned maxInstructions, bool isCompleted)
      : input(input, KTestDeleter), maxInstructions(maxInstructions),
        isCompleted(isCompleted), inputPosition(0) {
  }

  SeedInfo(std::string _path);

  KTestObject *getNextInput(const MemoryObject *mo, bool byName);

  /// Patch the seed so that condition is satisfied while retaining as
  /// many of the seed values as possible.
  void patchSeed(const ExecutionState &state, ref<Expr> condition,
                 TimingSolver *solver);
};
} // namespace klee

#endif /* KLEE_SEEDINFO_H */
