//===-- Constraints.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_CONSTRAINTS_H
#define KLEE_CONSTRAINTS_H

#include "klee/ADT/DisjointSetUnion.h"
#include "klee/ADT/PersistentArray.h"
#include "klee/Expr/Assignment.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprHashMap.h"
#include "klee/Expr/IndependentSet.h"

#include <set>

namespace klee {

class MemoryObject;

class IndependentConstraintSetUnion
    : public DisjointSetUnion<ref<Expr>, IndependentConstraintSet> {
public:
  using DisjointSetUnion<ref<Expr>, IndependentConstraintSet>::DisjointSetUnion;
  void getIndependentConstraints(const ref<Expr> &e,
                                 std::vector<ref<Expr>> &result) const {
    ref<IndependentConstraintSet> compare = new IndependentConstraintSet(e);

    for (size_t i = 0; i < capacity; i++) {
      size_t a = constFind(i);
      if (IndependentConstraintSet::intersects(disjointSets[a], compare)) {
        for (ref<Expr> expr : disjointSets[a]->exprs) {
          result.push_back(expr);
        }
      }
    }
  }
};

/// Resembles a set of constraints that can be passed around
///
class ConstraintSet {
  friend class ConstraintManager;

public:
  using constraints_ty = std::vector<ref<Expr>>;
  using iterator = constraints_ty::iterator;
  using const_iterator = constraints_ty::const_iterator;

  using constraint_iterator = const_iterator;

  bool empty() const;
  constraint_iterator begin() const;
  constraint_iterator end() const;
  size_t size() const noexcept;

  explicit ConstraintSet(constraints_ty cs);
  explicit ConstraintSet(ExprHashSet cs);
  ConstraintSet();

  void push_back(const ref<Expr> &e);
  void updateConcretization(const Assignment &symcretes);
  ConstraintSet withExpr(ref<Expr> e) const;

  std::vector<const Array *> gatherArrays() const;
  std::vector<const Array *> gatherSymcreteArrays() const;

  std::set<ref<Expr>> asSet() const;
  const Assignment &getConcretization() const;

  bool operator==(const ConstraintSet &b) const {
    return constraints == b.constraints;
  }

  bool operator<(const ConstraintSet &b) const {
    return constraints < b.constraints;
  }

  void dump() const;

  void getAllIndependentConstraintsSets(
      const ref<Expr> &queryExpr,
      std::vector<ref<IndependentConstraintSet>> &result) const;

  void getIndependentConstraints(const ref<Expr> &queryExpr,
                                 std::vector<ref<Expr>> &result) const;

private:
  constraints_ty constraints;
  Assignment concretization;
};

class ExprVisitor;

/// Manages constraints, e.g. optimisation
class ConstraintManager {
public:
  /// Create constrasize_t manager that modifies constraints
  /// \param constraints
  explicit ConstraintManager(ConstraintSet &constraints);

  /// Simplify expression expr based on constraints
  /// \param constraints set of constraints used for simplification
  /// \param expr to simplify
  /// \return simplified expression
  static ref<Expr> simplifyExpr(const ConstraintSet &constraints,
                                const ref<Expr> &expr,
                                ExprHashSet &conflictExpressions,
                                Expr::States &result);
  static ref<Expr> simplifyExpr(const ConstraintSet &constraints,
                                const ref<Expr> &expr);

  /// Add constrasize_t to the referenced constrasize_t set
  /// \param constraint
  void addConstraint(const ref<Expr> &constraint);
  void addConstraint(const ref<Expr> &constraint, const Assignment &symcretes);

private:
  /// Rewrite set of constraints using the visitor
  /// \param visitor constrasize_t rewriter
  /// \return true iff any constrasize_t has been changed
  bool rewriteConstraints(ExprVisitor &visitor);

  /// Add constrasize_t to the set of constraints
  void addConstraintInternal(const ref<Expr> &constraint);

  ConstraintSet &constraints;
};

} // namespace klee

#endif /* KLEE_CONSTRAINTS_H */
