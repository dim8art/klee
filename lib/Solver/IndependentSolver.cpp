//===-- IndependentSolver.cpp ---------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr/SymbolicSource.h"
#include "klee/Solver/SolverUtil.h"
#include "llvm/Support/Casting.h"
#define DEBUG_TYPE "independent-solver"
#include "klee/Solver/Solver.h"

#include "klee/Expr/Assignment.h"
#include "klee/Expr/Constraints.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/IndependentSet.h"
#include "klee/Solver/SolverImpl.h"
#include "klee/Support/Debug.h"

#include "llvm/Support/raw_ostream.h"

#include <list>
#include <map>
#include <ostream>
#include <vector>

using namespace klee;
using namespace llvm;

class IndependentSolver : public SolverImpl {
private:
  Solver *solver;

public:
  IndependentSolver(Solver *_solver) : solver(_solver) {}
  ~IndependentSolver() { delete solver; }

  bool computeTruth(const Query &, bool &isValid);
  bool computeValidity(const Query &, PartialValidity &result);
  bool computeValue(const Query &, ref<Expr> &result);
  bool computeInitialValues(const Query &query,
                            const std::vector<const Array *> &objects,
                            std::vector<SparseStorage<unsigned char>> &values,
                            bool &hasSolution);
  bool check(const Query &query, ref<SolverResponse> &result);
  bool computeValidityCore(const Query &query, ValidityCore &validityCore,
                           bool &isValid);
  SolverRunStatus getOperationStatusCode();
  char *getConstraintLog(const Query &);
  void setCoreSolverTimeout(time::Span timeout);
};

bool IndependentSolver::computeValidity(const Query &query,
                                        PartialValidity &result) {
  constraints_ty required;
  ref<const IndependentConstraintSet> eltsClosure =
      query.getIndependentConstraints(required);
  ConstraintSet tmp(
      required, eltsClosure->symcretes,
      query.constraints.concretization().part(eltsClosure->symcretes));
  return solver->impl->computeValidity(query.withConstraints(tmp), result);
}

bool IndependentSolver::computeTruth(const Query &query, bool &isValid) {
  constraints_ty required;
  ref<const IndependentConstraintSet> eltsClosure =
      query.getIndependentConstraints(required);
  ConstraintSet tmp(
      required, eltsClosure->symcretes,
      query.constraints.concretization().part(eltsClosure->symcretes));
  return solver->impl->computeTruth(query.withConstraints(tmp), isValid);
}

bool IndependentSolver::computeValue(const Query &query, ref<Expr> &result) {
  constraints_ty required;
  ref<const IndependentConstraintSet> eltsClosure =
      query.getIndependentConstraints(required);
  ConstraintSet tmp(
      required, eltsClosure->symcretes,
      query.constraints.concretization().part(eltsClosure->symcretes));
  return solver->impl->computeValue(query.withConstraints(tmp), result);
}

// Helper function used only for assertions to make sure point created
// during computeInitialValues is in fact correct. The ``retMap`` is used
// in the case ``objects`` doesn't contain all the assignments needed.
bool assertCreatedPointEvaluatesToTrue(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<SparseStorage<unsigned char>> &values,
    std::map<const Array *, SparseStorage<unsigned char>> &retMap) {
  // _allowFreeValues is set to true so that if there are missing bytes in the
  // assigment we will end up with a non ConstantExpr after evaluating the
  // assignment and fail
  Assignment assign = Assignment(objects, values, /*_allowFreeValues=*/true);

  // Add any additional bindings.
  // The semantics of std::map should be to not insert a (key, value)
  // pair if it already exists so we should continue to use the assignment
  // from ``objects`` and ``values``.
  if (retMap.size() > 0)
    assign.bindings.insert(retMap.begin(), retMap.end());

  for (auto const &constraint : query.constraints.cs()) {
    ref<Expr> ret = assign.evaluate(constraint);

    assert(isa<ConstantExpr>(ret) &&
           "assignment evaluation did not result in constant");
    ref<ConstantExpr> evaluatedConstraint = dyn_cast<ConstantExpr>(ret);
    if (evaluatedConstraint->isFalse()) {
      return false;
    }
  }
  ref<Expr> neg = Expr::createIsZero(query.expr);
  ref<Expr> q = assign.evaluate(neg);
  assert(isa<ConstantExpr>(q) &&
         "assignment evaluation did not result in constant");
  return cast<ConstantExpr>(q)->isTrue();
}

bool assertCreatedPointEvaluatesToTrue(
    const Query &query,
    std::map<const Array *, SparseStorage<unsigned char>> &bindings,
    std::map<const Array *, SparseStorage<unsigned char>> &retMap) {
  std::vector<const Array *> objects;
  std::vector<SparseStorage<unsigned char>> values;
  objects.reserve(bindings.size());
  values.reserve(bindings.size());
  for (auto &ovp : bindings) {
    objects.push_back(ovp.first);
    values.push_back(ovp.second);
  }
  return assertCreatedPointEvaluatesToTrue(query, objects, values, retMap);
}

bool IndependentSolver::computeInitialValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<SparseStorage<unsigned char>> &values, bool &hasSolution) {
  // We assume the query has a solution except proven differently
  // This is important in case we don't have any constraints but
  // we need initial values for requested array objects.
  hasSolution = true;
  std::vector<ref<const IndependentConstraintSet>> factors;
  query.getAllIndependentConstraintsSets(factors);

  // Used to rearrange all of the answers into the correct order
  Assignment retMap(true);
  for (ref<const IndependentConstraintSet> it : factors) {
    std::vector<const Array *> arraysInFactor;
    it->calculateArrayReferences(arraysInFactor);
    // Going to use this as the "fresh" expression for the Query() invocation
    // below
    assert(it->exprs.size() >= 1 && "No null/empty factors");
    if (arraysInFactor.size() == 0) {
      continue;
    }
    ConstraintSet tmp(it->exprs, it->symcretes,
                      query.constraints.concretization().part(it->symcretes));
    std::vector<SparseStorage<unsigned char>> tempValues;
    if (!solver->impl->computeInitialValues(
            Query(tmp, ConstantExpr::alloc(0, Expr::Bool)), arraysInFactor,
            tempValues, hasSolution)) {
      values.clear();

      return false;
    } else if (!hasSolution) {
      values.clear();

      return true;
    } else {
      assert(tempValues.size() == arraysInFactor.size() &&
             "Should be equal number arrays and answers");
      // We already have an array with some partially correct answers,
      // so we need to place the answers to the new query into the right
      // spot while avoiding the undetermined values also in the array
      it->addValuesToAssignment(arraysInFactor, tempValues, retMap);
    }
  }

  for (std::vector<const Array *>::const_iterator it = objects.begin();
       it != objects.end(); it++) {
    const Array *arr = *it;
    if (!retMap.bindings.count(arr)) {
      // this means we have an array that is somehow related to the
      // constraint, but whose values aren't actually required to
      // satisfy the query.
      ref<ConstantExpr> arrayConstantSize =
          dyn_cast<ConstantExpr>(retMap.evaluate(arr->size));
      assert(arrayConstantSize &&
             "Array of symbolic size had not receive value for size!");
      SparseStorage<unsigned char> ret(arrayConstantSize->getZExtValue());
      values.push_back(ret);
    } else {
      values.push_back(retMap.bindings[arr]);
    }
  }

  assert(assertCreatedPointEvaluatesToTrue(query, objects, values,
                                           retMap.bindings) &&
         "should satisfy the equation");

  return true;
}

bool IndependentSolver::check(const Query &query, ref<SolverResponse> &result) {
  // We assume the query has a solution except proven differently
  // This is important in case we don't have any constraints but
  // we need initial values for requested array objects.

  std::vector<ref<const IndependentConstraintSet>> factors;
  query.getAllIndependentConstraintsSets(factors);

  // Used to rearrange all of the answers into the correct order
  Assignment retMap;
  for (ref<const IndependentConstraintSet> it : factors) {
    std::vector<const Array *> arraysInFactor;
    it->calculateArrayReferences(arraysInFactor);
    // Going to use this as the "fresh" expression for the Query() invocation
    // below
    assert(it->exprs.size() >= 1 && "No null/empty factors");
    if (arraysInFactor.size() == 0) {
      continue;
    }

    constraints_ty factorConstraints = it->exprs;
    ref<Expr> factorExpr = ConstantExpr::alloc(0, Expr::Bool);
    auto factorConstraintsExprIterator =
        std::find(factorConstraints.begin(), factorConstraints.end(),
                  query.negateExpr().expr);
    if (factorConstraintsExprIterator != factorConstraints.end()) {
      factorConstraints.erase(factorConstraintsExprIterator);
      factorExpr = query.expr;
    }

    ref<SolverResponse> tempResult;
    std::vector<SparseStorage<unsigned char>> tempValues;
    if (!solver->impl->check(
            Query(ConstraintSet(
                      factorConstraints, it->symcretes,
                      query.constraints.concretization().part(it->symcretes)),
                  factorExpr),
            tempResult)) {
      return false;
    } else if (isa<ValidResponse>(tempResult)) {
      result = tempResult;
      return true;
    } else {
      bool success =
          tempResult->tryGetInitialValuesFor(arraysInFactor, tempValues);
      assert(success && "Can not get initial values (Independent solver)!");
      assert(tempValues.size() == arraysInFactor.size() &&
             "Should be equal number arrays and answers");
      // We already have an array with some partially correct answers,
      // so we need to place the answers to the new query into the right
      // spot while avoiding the undetermined values also in the array
      it->addValuesToAssignment(arraysInFactor, tempValues, retMap);
    }
  }
  result = new InvalidResponse(retMap.bindings);
  std::map<const Array *, SparseStorage<unsigned char>> bindings;
  bool success = result->tryGetInitialValues(bindings);
  assert(success);

  assert(assertCreatedPointEvaluatesToTrue(query, bindings, retMap.bindings) &&
         "should satisfy the equation");

  return true;
}

bool IndependentSolver::computeValidityCore(const Query &query,
                                            ValidityCore &validityCore,
                                            bool &isValid) {
  constraints_ty required;
  ref<const IndependentConstraintSet> eltsClosure =
      query.getIndependentConstraints(required);
  ConstraintSet tmp(
      required, eltsClosure->symcretes,
      query.constraints.concretization().part(eltsClosure->symcretes));
  return solver->impl->computeValidityCore(query.withConstraints(tmp),
                                           validityCore, isValid);
}

SolverImpl::SolverRunStatus IndependentSolver::getOperationStatusCode() {
  return solver->impl->getOperationStatusCode();
}

char *IndependentSolver::getConstraintLog(const Query &query) {
  return solver->impl->getConstraintLog(query);
}

void IndependentSolver::setCoreSolverTimeout(time::Span timeout) {
  solver->impl->setCoreSolverTimeout(timeout);
}

Solver *klee::createIndependentSolver(Solver *s) {
  return new Solver(new IndependentSolver(s));
}
