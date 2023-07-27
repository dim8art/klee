#ifndef KLEE_INDEPENDENTCONSTRAINTSETUNION_H
#define KLEE_INDEPENDENTCONSTRAINTSETUNION_H

#include "klee/Expr/Assignment.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/IndependentSet.h"

namespace klee {
class IndependentConstraintSetUnion
    : public DisjointSetUnion<ref<Expr>, IndependentConstraintSet,
                              util::ExprHash> {
public:
  Assignment concretization;

public:
  ExprHashMap<ref<Expr>> concretizedExprs;

public:
  void updateConcretization(const Assignment &c);
  void removeConcretization(const Assignment &remove);
  void reEvaluateConcretization(const Assignment &newConcretization);

  IndependentConstraintSetUnion getConcretizedVersion() const;
  IndependentConstraintSetUnion
  getConcretizedVersion(const Assignment &c) const;

  IndependentConstraintSetUnion();
  IndependentConstraintSetUnion(const constraints_ty &is,
                                const SymcreteOrderedSet &os,
                                const Assignment &c);

  ref<const IndependentConstraintSet>
  getIndependentConstraints(const ref<Expr> &e, constraints_ty &result) const;
  void getAllIndependentConstraintSets(
      const ref<Expr> &e,
      std::vector<ref<const IndependentConstraintSet>> &result) const;
  void addExpr(const ref<Expr> e);
  void addSymcrete(const ref<Symcrete> s);
};
} // namespace klee

#endif