#ifndef KLEE_INDEPENDENTSET_H
#define KLEE_INDEPENDENTSET_H

#include "klee/Expr/Expr.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <set>
#include <string>
#include <vector>
namespace klee {

template <class T> class DenseSet {
  typedef std::set<T> set_ty;
  set_ty s;

public:
  DenseSet() {}

  void add(T x) { s.insert(x); }
  void add(T start, T end) {
    for (; start < end; start++)
      s.insert(start);
  }

  // returns true iff set is changed by addition
  bool add(const DenseSet &b) {
    bool modified = false;
    for (typename set_ty::const_iterator it = b.s.begin(), ie = b.s.end();
         it != ie; ++it) {
      if (modified || !s.count(*it)) {
        modified = true;
        s.insert(*it);
      }
    }
    return modified;
  }

  bool intersects(const DenseSet &b) {
    for (typename set_ty::iterator it = s.begin(), ie = s.end(); it != ie; ++it)
      if (b.s.count(*it))
        return true;
    return false;
  }

  std::set<unsigned>::iterator begin() { return s.begin(); }

  std::set<unsigned>::iterator end() { return s.end(); }

  void print(llvm::raw_ostream &os) const {
    bool first = true;
    os << "{";
    for (typename set_ty::iterator it = s.begin(), ie = s.end(); it != ie;
         ++it) {
      if (first) {
        first = false;
      } else {
        os << ",";
      }
      os << *it;
    }
    os << "}";
  }
};

template <class T>
inline llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const DenseSet<T> &dis) {
  dis.print(os);
  return os;
}

class ObjectsSet {
public:
  typedef std::map<const Array *, DenseSet<unsigned>> elements_ty;
  elements_ty
      elements;     // Represents individual elements of array accesses (arr[1])
  std::set<const Array *>
      wholeObjects; // Represents symbolically accessed arrays (arr[x])
  ref<Expr> expr;   // Expression that is associated with this set
  ObjectsSet();
  ObjectsSet(ref<Expr> e);
  ObjectsSet(const ObjectsSet &ies);

  ObjectsSet &operator=(const ObjectsSet &ies);

  void print(llvm::raw_ostream &os) const;

  // more efficient when this is the smaller set
  bool intersects(const ObjectsSet &b);
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const ObjectsSet &ies) {
  ies.print(os);
  return os;
}

// Extracts which arrays are referenced from a particular objects set.
// Examines both the actual known array accesses arr[1] plus the undetermined
// accesses arr[x].
void calculateArrayReferences(const std::vector<ObjectsSet> &independentSet,
                              std::vector<const Array *> &returnVector);

void calculateExprReferences(const std::vector<ObjectsSet> &independentSet,
                             std::vector<ref<Expr>> &returnVector);

void calculateElementReferences(const std::vector<ObjectsSet> &independentSet,
                                ObjectsSet::elements_ty &returnVector);
} // namespace klee

#endif /* KLEE_INDEPENDENTSET_H */
