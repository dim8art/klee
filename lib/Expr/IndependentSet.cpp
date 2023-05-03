#include "klee/Expr/IndependentSet.h"

#include "klee/Expr/ExprUtil.h"
#include "klee/Expr/SymbolicSource.h"
#include "klee/Solver/Solver.h"

#include <list>
#include <map>
#include <set>
#include <vector>

namespace klee {

ObjectsSet::ObjectsSet() {}

ObjectsSet::ObjectsSet(ref<Expr> e) {
  expr = e;
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

ObjectsSet::ObjectsSet(const ObjectsSet &ies)
    : elements(ies.elements), wholeObjects(ies.wholeObjects), expr(ies.expr) {}

ObjectsSet &ObjectsSet::operator=(const ObjectsSet &ies) {
  elements = ies.elements;
  wholeObjects = ies.wholeObjects;
  expr = ies.expr;
  return *this;
}

void ObjectsSet::print(llvm::raw_ostream &os) const {
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

// more efficient when this is the smaller set
bool ObjectsSet::intersects(const ObjectsSet &b) {
  // If there are any symbolic arrays in our query that b accesses
  for (std::set<const Array *>::iterator it = wholeObjects.begin(),
                                         ie = wholeObjects.end();
       it != ie; ++it) {
    const Array *array = *it;
    if (b.wholeObjects.count(array) ||
        b.elements.find(array) != b.elements.end())
      return true;
  }
  for (elements_ty::iterator it = elements.begin(), ie = elements.end();
       it != ie; ++it) {
    const Array *array = it->first;
    // if the array we access is symbolic in b
    if (b.wholeObjects.count(array))
      return true;
    elements_ty::const_iterator it2 = b.elements.find(array);
    // if any of the elements we access are also accessed by b
    if (it2 != b.elements.end()) {
      if (it->second.intersects(it2->second))
        return true;
    }
  }
  return false;
}

void calculateArrayReferences(const std::vector<ObjectsSet> &independentSet,
                              std::vector<const Array *> &returnVector) {
  for (ObjectsSet ie : independentSet) {
    std::set<const Array *> thisSeen;
    for (ObjectsSet::elements_ty::const_iterator it = ie.elements.begin();
         it != ie.elements.end(); it++) {
      thisSeen.insert(it->first);
    }
    for (std::set<const Array *>::iterator it = ie.wholeObjects.begin();
         it != ie.wholeObjects.end(); it++) {
      thisSeen.insert(*it);
    }
    for (std::set<const Array *>::iterator it = thisSeen.begin();
         it != thisSeen.end(); it++) {
      returnVector.push_back(*it);
    }
  }
}

void calculateExprReferences(const std::vector<ObjectsSet> &independentSet,
                             std::vector<ref<Expr>> &returnVector) {
  for (ObjectsSet ie : independentSet) {
    returnVector.push_back(ie.expr);
  }
}

void calculateElementReferences(const std::vector<ObjectsSet> &independentSet,
                                ObjectsSet::elements_ty &returnVector) {
  std::set<const Array *> wholeObjects;
  ObjectsSet::elements_ty elements;
  for (ObjectsSet ie : independentSet) {
    for (std::set<const Array *>::const_iterator it = ie.wholeObjects.begin();
         it != ie.wholeObjects.end(); it++) {
      const Array *array = *it;
      ObjectsSet::elements_ty::iterator it2 = elements.find(array);
      if (it2 != elements.end()) {
        elements.erase(it2);
        wholeObjects.insert(array);
      } else {
        if (!wholeObjects.count(array)) {
          wholeObjects.insert(array);
        }
      }
    }
    for (ObjectsSet::elements_ty::const_iterator it = ie.elements.begin();
         it != ie.elements.end(); it++) {
      const Array *array = it->first;
      if (!wholeObjects.count(array)) {
        ObjectsSet::elements_ty::iterator it2 = elements.find(array);
        if (it2 == elements.end()) {
          elements.insert(*it);
        } else {
          it2->second.add(it->second);
        }
      }
    }
  }
  returnVector = elements;
}

} // namespace klee
