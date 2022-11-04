//===-- Constraints.cpp ---------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Expr/Constraints.h"

#include "klee/Expr/ExprVisitor.h"
#include "klee/Module/KModule.h"
#include "klee/Support/OptionCategories.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/CommandLine.h"
#include <fstream>
#include <map>

using namespace klee;

namespace {
llvm::cl::opt<bool> RewriteEqualities(
    "rewrite-equalities",
    llvm::cl::desc("Rewrite existing constraints when an equality with a "
                   "constant is added (default=true)"),
    llvm::cl::init(true),
    llvm::cl::cat(SolvingCat));
} // namespace

class ExprReplaceVisitor : public ExprVisitor {
private:
  ref<Expr> src, dst;

public:
  ExprReplaceVisitor(const ref<Expr> &_src, const ref<Expr> &_dst)
      : src(_src), dst(_dst) {}

  Action visitExpr(const Expr &e) override {
    if (e == *src) {
      return Action::changeTo(dst);
    }
    return Action::doChildren();
  }

  Action visitExprPost(const Expr &e) override {
    if (e == *src) {
      return Action::changeTo(dst);
    }
    return Action::doChildren();
  }
};

class ExprReplaceVisitor2 : public ExprVisitor {
private:
  std::map<ref<Expr>, ref<Expr>> &replacements;
  std::set<ref<Expr>> &conflictExpressions;
  Expr::States &result;

public:
  explicit ExprReplaceVisitor2(std::map<ref<Expr>, ref<Expr>> &_replacements,
                               std::set<ref<Expr>> &_conflictExpressions,
                               Expr::States &_result)

      : ExprVisitor(true), replacements(_replacements),
        conflictExpressions(_conflictExpressions), result(_result) {}

  Action visitExprPost(const Expr &e) override {
    auto it = replacements.find(ref<Expr>(const_cast<Expr *>(&e)));
    if (it != replacements.end()) {
      ref<Expr> equality = EqExpr::create(it->first, it->second);
      conflictExpressions.insert(equality);
      return Action::changeTo(it->second);
    }
    return Action::doChildren();
  }
  ref<Expr> findConflict(const ref<Expr> &e) {
    ref<Expr> eSimplified = visit(e);
    result = Expr::States::Undefined;

    if (conflictExpressions.find(ConstantExpr::alloc(0, Expr::Bool)) !=
        conflictExpressions.end()) {
      result = Expr::States::False;
      conflictExpressions.clear();
      conflictExpressions.insert(ConstantExpr::alloc(0, Expr::Bool));
      return eSimplified;
    }
    if (conflictExpressions.find(ConstantExpr::alloc(1, Expr::Bool)) !=
        conflictExpressions.end()) {
      conflictExpressions.erase(ConstantExpr::alloc(1, Expr::Bool)); //
    }
    if (eSimplified->getWidth() != Expr::Bool ||
        !isa<ConstantExpr>(*eSimplified)) {
      conflictExpressions.clear();
      return eSimplified;
    }
    if (eSimplified->isTrue() == true)
      result = Expr::States::True;

    if (eSimplified->isFalse() == true)
      result = Expr::States::False;
    return eSimplified;
  }
};

bool ConstraintManager::rewriteConstraints(ExprVisitor &visitor) {
  ConstraintSet old;
  bool changed = false;

  std::swap(constraints, old);
  for (auto &ce : old) {
    ref<Expr> e = visitor.visit(ce);

    if (e!=ce) {
      addConstraintInternal(e); // enable further reductions
      changed = true;
    } else {
      constraints.push_back(ce);
    }
  }

  return changed;
}

ref<Expr> ConstraintManager::simplifyExpr(const ConstraintSet &constraints,
                                          const ref<Expr> &e) {
  std::set<ref<Expr>> cE;
  Expr::States r;
  return simplifyExpr(constraints, e, cE, r);
}

ref<Expr> ConstraintManager::simplifyExpr(
    const ConstraintSet &constraints, const ref<Expr> &e,
    std::set<ref<Expr>> &conflictExpressions, Expr::States &result) {

  if (isa<ConstantExpr>(e))
    return e;

  std::map<ref<Expr>, ref<Expr>> equalities;

  for (auto &constraint : constraints) {
    if (const EqExpr *ee = dyn_cast<EqExpr>(constraint)) {
      if (isa<ConstantExpr>(ee->left)) {
        equalities.insert(std::make_pair(ee->right, ee->left));
      } else {
        equalities.insert(
            std::make_pair(constraint, ConstantExpr::alloc(1, Expr::Bool)));
      }
    } else {
      equalities.insert(
          std::make_pair(constraint, ConstantExpr::alloc(1, Expr::Bool)));
    }
  }

  // Test if found conflictExpressions are the set of expressions that causes
  // the conflict

  ExprReplaceVisitor2(equalities, conflictExpressions, result).findConflict(e);
  std::map<ref<Expr>, ref<Expr>> equalitiesTest;
  std::set<ref<Expr>> conflictExpressionsTest;
  Expr::States resultTest;

  for (auto &constraint : conflictExpressions) {
    if (const EqExpr *ee = dyn_cast<EqExpr>(constraint))
      equalitiesTest.insert(std::make_pair(ee->right, ee->left));
    else
      assert(false);
  }

  ExprReplaceVisitor2(equalitiesTest, conflictExpressionsTest, resultTest)
      .findConflict(e);
  //
  if (conflictExpressions !=
      std::set<ref<Expr>>({ConstantExpr::alloc(0, Expr::Bool)}))
    assert(resultTest == result);
  // Found expressions should return same result
  //

  // Test if after erasing element we would not have conflicted result
  for (auto &constraint1 : conflictExpressions) {
    equalitiesTest.clear();
    conflictExpressionsTest.clear();
    for (auto &constraint : conflictExpressions) {
      if (constraint1 == constraint)
        continue;
      if (const EqExpr *ee = dyn_cast<EqExpr>(constraint))
        equalitiesTest.insert(std::make_pair(ee->right, ee->left));
    }

    ExprReplaceVisitor2(equalitiesTest, conflictExpressionsTest, resultTest)
        .findConflict(e);
    assert(resultTest == Expr::States::Undefined);
  }
  //
  return ExprReplaceVisitor2(equalities, conflictExpressions, result)
      .findConflict(e);
}

void ConstraintManager::addConstraintInternal(const ref<Expr> &e) {
  // rewrite any known equalities and split Ands into different conjuncts

  switch (e->getKind()) {
  case Expr::Constant:
    assert(cast<ConstantExpr>(e)->isTrue() &&
           "attempt to add invalid (false) constraint");
    break;

    // split to enable finer grained independence and other optimizations
  case Expr::And: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    addConstraintInternal(be->left);
    addConstraintInternal(be->right);
    break;
  }

  case Expr::Eq: {
    if (RewriteEqualities) {
      // XXX: should profile the effects of this and the overhead.
      // traversing the constraints looking for equalities is hardly the
      // slowest thing we do, but it is probably nicer to have a
      // ConstraintSet ADT which efficiently remembers obvious patterns
      // (byte-constant comparison).
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (isa<ConstantExpr>(be->left)) {
        ExprReplaceVisitor visitor(be->right, be->left);
        rewriteConstraints(visitor);
      }
    }
    constraints.push_back(e);
    break;
  }

  default:
    constraints.push_back(e);
    break;
  }
}

void ConstraintManager::addConstraint(const ref<Expr> &e) {
  ref<Expr> simplified = simplifyExpr(constraints, e);
  addConstraintInternal(simplified);
}

ConstraintManager::ConstraintManager(ConstraintSet &_constraints)
    : constraints(_constraints) {}

bool ConstraintSet::empty() const { return constraints.empty(); }

klee::ConstraintSet::constraint_iterator ConstraintSet::begin() const {
  return constraints.begin();
}

klee::ConstraintSet::constraint_iterator ConstraintSet::end() const {
  return constraints.end();
}

size_t ConstraintSet::size() const noexcept { return constraints.size(); }

void ConstraintSet::push_back(const ref<Expr> &e) { constraints.push_back(e); }
