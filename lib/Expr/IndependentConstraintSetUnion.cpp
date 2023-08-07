#include "klee/Expr/IndependentConstraintSetUnion.h"

namespace klee {

IndependentConstraintSetUnion::IndependentConstraintSetUnion()
    : concretization(Assignment(true)) {}

IndependentConstraintSetUnion::IndependentConstraintSetUnion(
    const constraints_ty &is, const SymcreteOrderedSet &os, const Assignment &c)
    : concretization(Assignment(true)) {
  for (ref<Expr> e : is) {
    addValue(e);
  }
  for (ref<Symcrete> s : os) {
    addSymcrete(s);
  }
  updateConcretization(c);
}

IndependentConstraintSetUnion::IndependentConstraintSetUnion(ref<const IndependentConstraintSet> ics) {
  for(ref<Expr> e : ics->exprs){
    rank = rank.replace({e, 0});
    internalStorage = internalStorage.insert(e);
    disjointSets = disjointSets.replace({e, nullptr});
  }

  for(ref<Symcrete> s : ics->symcretes){
    ref<Expr> e = s->symcretized;
    rank = rank.replace({e, 0});
    internalStorage = internalStorage.insert(e);
    disjointSets = disjointSets.replace({e, nullptr});
  }

  if(internalStorage.size() == 0){
    return;
  }

  ref<Expr> first = *(internalStorage.begin());
  for(ref<Expr> e : internalStorage){
    parent  = parent.replace({e, first});
  }
  rank = rank.replace({first, 1});
  roots = roots.insert(first); 
  disjointSets = disjointSets.replace({first, ics});
  concretization = ics->concretization;
}

void IndependentConstraintSetUnion::updateConcretization(
    const Assignment &delta) {
  for (ref<Expr> e : roots) {
    ref<const IndependentConstraintSet> ics = disjointSets.at(e);
    Assignment part = delta.part(ics->symcretes);
    ics = ics->updateConcretization(part, concretizedExprs);
    disjointSets = disjointSets.replace({e, ics});
  }
  for (auto it : delta.bindings) {
    concretization.bindings =
        concretization.bindings.replace({it.first, it.second});
  }
}

void IndependentConstraintSetUnion::removeConcretization(
    const Assignment &remove) {
  for (ref<Expr> e : roots) {
    ref<const IndependentConstraintSet> ics = disjointSets.at(e);
    Assignment part = remove.part(ics->symcretes);
    ics = ics->removeConcretization(part, concretizedExprs);
    disjointSets = disjointSets.replace({e, ics});
  }
  for (auto it : remove.bindings) {
    concretization.bindings = concretization.bindings.remove(it.first);
  }
}
void IndependentConstraintSetUnion::reEvaluateConcretization(
    const Assignment &newConcretization) {
  Assignment delta(true);
  Assignment removed(true);
  for (const auto it : concretization) {
    if (newConcretization.bindings.count(it.first) == 0) {
      removed.bindings = removed.bindings.insert(it);
    } else if (newConcretization.bindings.at(it.first) != it.second) {
      delta.bindings =
          delta.bindings.insert(*(newConcretization.bindings.find(it.first)));
    }
  }
  updateConcretization(delta);
  removeConcretization(removed);
}

ref<const IndependentConstraintSet>
IndependentConstraintSetUnion::getIndependentConstraints(
    const ref<Expr> &e, constraints_ty &result) const {
  ref<const IndependentConstraintSet> compare = new IndependentConstraintSet(e);
  ref<const IndependentConstraintSet> add  = new IndependentConstraintSet();
  for (ref<Expr> i : internalStorage) {
    ref<Expr> a = constFind(i);
    ref<const IndependentConstraintSet> ics = disjointSets.at(a);
    if (IndependentConstraintSet::intersects(ics, compare)) {
      add = IndependentConstraintSet::merge(ics, add);
    }
  }
  return add;
}

void IndependentConstraintSetUnion::getAllIndependentConstraintSets(
    const ref<Expr> &e,
    std::vector<ref<const IndependentConstraintSet>> &result) const {
  IndependentConstraintSetUnion u = *this;
  ConstantExpr *CE = dyn_cast<ConstantExpr>(e);
  ref<Expr> neg;
  if (CE) {
    assert(CE && CE->isFalse() &&
           "the expr should always be false and "
           "therefore not included in factors");
    u.getAllIndependentSets(result);
  } else {
    // factor that contains query expr should be checked first
    neg = Expr::createIsZero(e);
    u.addExpr(neg);
    result.push_back(u.findGroup(neg));
    for (ref<Expr> v : u.roots) {
      if (!u.areJoined(v, neg)) {
        result.push_back(u.findGroup(v));
      }
    }
  }
}
void IndependentConstraintSetUnion::addExpr(const ref<Expr> e) {
  addValue(e);
  disjointSets =
      disjointSets.replace({find(e), disjointSets.at(find(e))->addExpr(e)});
}
void IndependentConstraintSetUnion::addSymcrete(const ref<Symcrete> s) {
  ref<Expr> value = s->symcretized;
  if (internalStorage.find(value) != internalStorage.end()) {
    return;
  }
  parent = parent.insert({value, value});
  roots = roots.insert(value);
  rank = rank.insert({value, 0});
  disjointSets = disjointSets.insert({value, new IndependentConstraintSet(s)});

  internalStorage = internalStorage.insert(value);
  internalStorage_ty oldRoots = roots;
  for (ref<Expr> v : oldRoots) {
    if (!areJoined(v, value) &&
        IndependentConstraintSet::intersects(disjointSets.at(find(v)),
                                             disjointSets.at(find(value)))) {
      merge(v, value);
    }
  }
  disjointSets = disjointSets.replace(
      {find(value), disjointSets.at(find(value))->addExpr(value)});
}


IndependentConstraintSetUnion
IndependentConstraintSetUnion::getConcretizedVersion() const {
  IndependentConstraintSetUnion icsu;
  for (ref<Expr> i : roots) {
    ref<const IndependentConstraintSet> root = disjointSets.at(i);
    icsu.add(root->concretizedSets);
  }
  icsu.concretizedExprs = concretizedExprs;
  return icsu;
}

IndependentConstraintSetUnion
IndependentConstraintSetUnion::getConcretizedVersion(
    const Assignment &newConcretization) const {
  IndependentConstraintSetUnion icsu = *this;
  icsu.reEvaluateConcretization(newConcretization);
  return icsu.getConcretizedVersion();
}
} // namespace klee