#include "klee/Expr/IndependentSet.h"

#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/SymbolicSource.h"
#include "klee/Solver/Solver.h"

#include <list>
#include <map>
#include <set>
#include <vector>

namespace klee {

IndependentConstraintSet::IndependentConstraintSet() {}

IndependentConstraintSet::IndependentConstraintSet(ref<Expr> e) {
  exprs.push_back(e);
  // Track all reads in the program.  Determines whether reads are
  // concrete or symbolic.  If they are symbolic, "collapses" array
  // by adding it to wholeObjects.  Otherwise, creates a mapping of
  // the form Map<array, set<index>> which tracks which parts of the
  // array are being accessed.
  std::vector<ref<ReadExpr>> reads;
  findReads(e, /* visitUpdates= */ true, reads);
  for (unsigned i = 0; i != reads.size(); ++i) {
    ReadExpr *re = reads[i].get();
    const Array *array = re->updates.root;

    // Reads of a constant array don't alias.
    if (re->updates.root->isConstantArray() && !re->updates.head)
      continue;

    if (!wholeObjects.count(array)) {
      if (ref<SymbolicAllocationSource> allocSource =
              dyn_cast_or_null<SymbolicAllocationSource>(array->source)) {
        wholeObjects.insert(array);
        wholeObjects.insert(allocSource->linkedArray);
      }

      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
        // if index constant, then add to set of constraints operating
        // on that array (actually, don't add constraint, just set index)
        DenseSet<unsigned> &dis = elements[array];
        dis.add((unsigned)CE->getZExtValue(32));
      } else {
        elements_ty::iterator it2 = elements.find(array);
        if (it2 != elements.end())
          elements.erase(it2);
        wholeObjects.insert(array);
      }
    }
  }
}

IndependentConstraintSet::IndependentConstraintSet(
    const ref<IndependentConstraintSet> &ics)
    : elements(ics->elements), wholeObjects(ics->wholeObjects),
      exprs(ics->exprs) {}

IndependentConstraintSet &
IndependentConstraintSet::operator=(const IndependentConstraintSet &ics) {
  elements = ics.elements;
  wholeObjects = ics.wholeObjects;
  exprs = ics.exprs;
  return *this;
}

void IndependentConstraintSet::print(llvm::raw_ostream &os) const {
  os << "{";
  bool first = true;
  for (std::set<const Array *>::iterator it = wholeObjects.begin(),
                                         ie = wholeObjects.end();
       it != ie; ++it) {
    const Array *array = *it;

    if (first) {
      first = false;
    } else {
      os << ", ";
    }

    os << "MO" << array->name;
  }
  for (elements_ty::const_iterator it = elements.begin(), ie = elements.end();
       it != ie; ++it) {
    const Array *array = it->first;
    const DenseSet<unsigned> &dis = it->second;

    if (first) {
      first = false;
    } else {
      os << ", ";
    }

    os << "MO" << array->name << " : " << dis;
  }
  os << "}";
}

bool IndependentConstraintSet::intersects(ref<IndependentConstraintSet> a,
                                          ref<IndependentConstraintSet> b) {
  if (a->exprs.size() > b->exprs.size()) {
    std::swap(a, b);
  }
  for (std::set<const Array *>::iterator it = a->wholeObjects.begin(),
                                         ie = a->wholeObjects.end();
       it != ie; ++it) {
    const Array *array = *it;
    if (b->wholeObjects.count(array) ||
        b->elements.find(array) != b->elements.end())
      return true;
  }
  for (elements_ty::iterator it = a->elements.begin(), ie = a->elements.end();
       it != ie; ++it) {
    const Array *array = it->first;
    // if the array we access is symbolic in b
    if (b->wholeObjects.count(array))
      return true;
    elements_ty::const_iterator it2 = b->elements.find(array);
    // if any of the elements we access are also accessed by b
    if (it2 != b->elements.end()) {
      if (it->second.intersects(it2->second)) {
        return true;
      }
    }
  }
  return false;
}

ref<IndependentConstraintSet>
IndependentConstraintSet::merge(ref<IndependentConstraintSet> a,
                                ref<IndependentConstraintSet> b) {
  // a = new IndependentConstraintSet(a);
  // b = new IndependentConstraintSet(b);
  // Right now we dont need do copy a and b because we always have only one
  // instance od DSU, but we will need it after adding DSU to constraints
  if (a->exprs.size() > b->exprs.size()) {
    std::swap(a, b);
  }
  for (unsigned i = 0; i < b->exprs.size(); i++) {
    ref<Expr> expr = b->exprs[i];
    a->exprs.push_back(expr);
  }

  for (std::set<const Array *>::const_iterator it = b->wholeObjects.begin(),
                                               ie = b->wholeObjects.end();
       it != ie; ++it) {
    const Array *array = *it;
    elements_ty::iterator it2 = a->elements.find(array);
    if (it2 != a->elements.end()) {
      a->elements.erase(it2);
      a->wholeObjects.insert(array);
    } else {
      if (!a->wholeObjects.count(array)) {
        a->wholeObjects.insert(array);
      }
    }
  }
  for (elements_ty::const_iterator it = b->elements.begin(),
                                   ie = b->elements.end();
       it != ie; ++it) {
    const Array *array = it->first;
    if (!a->wholeObjects.count(array)) {
      elements_ty::iterator it2 = a->elements.find(array);
      if (it2 == a->elements.end()) {
        a->elements.insert(*it);
      } else {
        it2->second.add(it->second);
      }
    }
  }
  return a;
}

void calculateArrayReferences(const ref<IndependentConstraintSet> ie,
                              std::vector<const Array *> &returnVector) {
  std::set<const Array *> thisSeen;
  for (IndependentConstraintSet::elements_ty::const_iterator it =
           ie->elements.begin();
       it != ie->elements.end(); it++) {
    thisSeen.insert(it->first);
  }
  for (std::set<const Array *>::iterator it = ie->wholeObjects.begin();
       it != ie->wholeObjects.end(); it++) {
    thisSeen.insert(*it);
  }
  for (std::set<const Array *>::iterator it = thisSeen.begin();
       it != thisSeen.end(); it++) {
    returnVector.push_back(*it);
  }
}

} // namespace klee
