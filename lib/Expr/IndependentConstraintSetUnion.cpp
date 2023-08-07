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
    rank[e] = 0;
    internalStorage.insert(e);
    disjointSets[e] = nullptr;
  }
  for(ref<Symcrete> s : ics->symcretes){
    rank[s->symcretized] = 0;
    internalStorage.insert(s->symcretized);
    disjointSets[s->symcretized] = nullptr;
  }

  if(internalStorage.size() == 0){
    return;
  }
  ref<Expr> first = *(internalStorage.begin());
  for(ref<Expr> e : internalStorage){
    parent[e] = first;
  }
  rank[first] = 1;
  roots.insert(first); 
  disjointSets[first] = ics;
  concretization = ics->concretization;
}

void IndependentConstraintSetUnion::updateConcretization(
    const Assignment &delta) {
  for (ref<Expr> e : roots) {
    ref<const IndependentConstraintSet> &ics = disjointSets[e];
    Assignment part = delta.part(ics->symcretes);
    ics = ics->updateConcretization(part, concretizedExprs);
  }
  for (auto it : delta.bindings) {
    concretization.bindings[it.first] = it.second;
  }
}

void IndependentConstraintSetUnion::removeConcretization(
    const Assignment &remove) {
  for (ref<Expr> e : roots) {
    ref<const IndependentConstraintSet> &ics = disjointSets[e];
    Assignment part = remove.part(ics->symcretes);
    ics = ics->removeConcretization(part, concretizedExprs);
  }
  for (auto it : remove.bindings) {
    concretization.bindings.erase(it.first);
  }
}
void IndependentConstraintSetUnion::reEvaluateConcretization(
    const Assignment &newConcretization) {
  Assignment delta(true);
  Assignment removed(true);
  for (const auto it : concretization) {
    if (newConcretization.bindings.count(it.first) == 0) {
      removed.bindings.insert(it);
    } else if (newConcretization.bindings.at(it.first) != it.second) {
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
    ref<const IndependentConstraintSet> ics = disjointSets.find(a)->second;
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
  disjointSets[find(e)] = disjointSets[find(e)]->addExpr(e);
}
void IndependentConstraintSetUnion::addSymcrete(const ref<Symcrete> s) {
  ref<Expr> value = s->symcretized;
  if (internalStorage.find(value) != internalStorage.end()) {
    return;
  }
  parent[value] = value;
  roots.insert(value);
  rank[value] = 0;
  disjointSets[value] = new IndependentConstraintSet(s);

  internalStorage.insert(value);
  std::vector<ref<Expr>> oldRoots(roots.begin(), roots.end());
  for (ref<Expr> v : oldRoots) {
    if (!areJoined(v, value) &&
        IndependentConstraintSet::intersects(disjointSets[find(v)],
                                             disjointSets[find(value)])) {
      merge(v, value);
    }
  }
  disjointSets[find(value)] = disjointSets[find(value)]->addExpr(value);
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