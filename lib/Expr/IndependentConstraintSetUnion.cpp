#include "klee/Expr/IndependentConstraintSetUnion.h"

namespace klee {
void IndependentConstraintSetUnion::updateConcretization(const Assignment &delta) {
  for (ref<Expr> e : roots) {
    std::map<ref<Expr>, ref<Expr>> updatedExprs;
    ref<const IndependentConstraintSet>& ics = disjointSets[e];
    Assignment part = delta.part(ics->symcretes);
    ics = ics->updateConcretization(part, updatedExprs);
    for(auto it : updatedExprs){
      concretizedExprs[it.first] = it.second;
    }
  }
  for (auto it : delta.bindings) {
    concretization.bindings[it.first] = it.second;
  }
  
}

void IndependentConstraintSetUnion::removeConcretization(const Assignment &remove) {
  for (ref<Expr> e : roots) {
    std::map<ref<Expr>, ref<Expr>> updatedExprs;
    ref<const IndependentConstraintSet>& ics = disjointSets[e];
    Assignment part = remove.part(ics->symcretes);
    ics = ics->removeConcretization(part, updatedExprs);
    for(auto it : updatedExprs){
      concretizedExprs[it.first] = it.second;
    }
  }
  for (auto it : remove.bindings) {
    concretization.bindings.erase(it.first);
  }
  
}
void IndependentConstraintSetUnion::reEvaluateConcretization(const Assignment &newConcretization){
  Assignment delta(true);
  Assignment removed(true);
  for (const auto it : concretization) {
    if (newConcretization.bindings.count(it.first) == 0) {
      removed.bindings.insert(it);
    } else if(newConcretization.bindings.at(it.first) != it.second){
      delta.bindings.insert(*(newConcretization.bindings.find(it.first)));
    }
  }
  updateConcretization(delta);
  removeConcretization(removed);
}
IndependentConstraintSetUnion::IndependentConstraintSetUnion() : concretization(Assignment(true)){}

ref<const IndependentConstraintSet>
IndependentConstraintSetUnion::getIndependentConstraints(
    const ref<Expr> &e, constraints_ty &result) const {
  ref<const IndependentConstraintSet> compare = new IndependentConstraintSet(e);

  for (ref<Expr> i : internalStorage) {
    ref<Expr> a = constFind(i);
    ref<const IndependentConstraintSet> ics = disjointSets.find(a)->second;
    if (IndependentConstraintSet::intersects(ics, compare)) {
      for (ref<Expr> expr : ics->exprs) {
        result.insert(expr);
      }
    }
  }
  return compare;
}
void IndependentConstraintSetUnion::addExpr(const ref<Expr> e){
  addValue(e);
  disjointSets[find(e)] = disjointSets[find(e)]->update();
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
    if (!joined(v, value) &&
        IndependentConstraintSet::intersects(disjointSets[find(v)],
                                             disjointSets[find(value)])) {
      merge(v, value);
    }
  }
  disjointSets[find(value)] = disjointSets[find(value)]->update();
}

IndependentConstraintSetUnion::IndependentConstraintSetUnion(
    const constraints_ty &is, const SymcreteOrderedSet &os,
    const Assignment &c) : concretization(Assignment(true)) {
  for (ref<Expr> e : is) {
    addValue(e);
  }
  for (ref<Symcrete> s : os) {
    addSymcrete(s);
  }
  updateConcretization(c);
}

IndependentConstraintSetUnion
IndependentConstraintSetUnion::getConcretizedVersion() const {
  IndependentConstraintSetUnion icsu;
  for (ref<Expr> i : roots) {
    ref<const IndependentConstraintSet> root = disjointSets.find(i)->second;
    icsu.add(root->concretizedSets);
  }
  return icsu; 
}

IndependentConstraintSetUnion
IndependentConstraintSetUnion::getConcretizedVersion(const Assignment &newConcretization) const {
  IndependentConstraintSetUnion icsu = *this;
  icsu.reEvaluateConcretization(newConcretization);
  return icsu.getConcretizedVersion();
}
} // namespace klee