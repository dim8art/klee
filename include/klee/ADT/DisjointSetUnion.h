#ifndef KLEE_DISJOINEDSETUNION_H
#define KLEE_DISJOINEDSETUNION_H
#include "klee/ADT/Ref.h"
#include "klee/ADT/ImmutableMap.h"
#include "klee/ADT/ImmutableSet.h"
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace klee {

template <typename ValueType, typename SetType, typename CMP = std::less<ValueType>>
class DisjointSetUnion {
protected:
  ImmutableMap<ValueType, ValueType, CMP> parent;
  ImmutableSet<ValueType, CMP> roots;
  ImmutableMap<ValueType, size_t, CMP> rank;

  ImmutableSet<ValueType, CMP> internalStorage;
  ImmutableMap<ValueType, ref<const SetType>, CMP> disjointSets;

  ValueType find(const ValueType &v) { // findparent
    assert(parent.find(v) != parent.end());
    if (v == parent.at(v))
      return v;
    parent = parent.replace(v, find(parent.at(v)));
    return parent.at(v);
  }

  ValueType constFind(const ValueType &v) const {
    assert(parent.find(v) != parent.end());
    ValueType v1 = parent.at(v);
    if (v == v1)
      return v;
    return constFind(v1);
  }

  void merge(ValueType a, ValueType b) {
    a = find(a);
    b = find(b);
    if (a == b) {
      return;
    }

    if (rank.at(a) < rank.at(b)) {
      std::swap(a, b);
    }
    parent = parent.replace(b, a);
    if (rank.at(a) == rank.at(b)) {
      rank = rank.replace(rank.at(a), rank.at(a)+1);
    }

    roots = roots.remove(b);
    disjointSets = disjointSets.replace(a, SetType::merge(disjointSets.at(a), disjointSets.at(b)));
    disjointSets = disjointSets.replace(b, nullptr);
  }

  bool areJoined(const ValueType &i, const ValueType &j) const {
    return constFind(i) == constFind(j);
  }

public:
  using internalStorage_ty = ImmutableSet<ValueType, CMP>;
  using disjointSets_ty =
      ImmutableMap<ValueType, ref<const SetType>, CMP>;
  using iterator = typename internalStorage_ty::iterator;

  iterator begin() const { return internalStorage.begin(); }
  iterator end() const { return internalStorage.end(); }

  size_t numberOfValues() const noexcept { return internalStorage.size(); }

  size_t numberOfGroups() const noexcept { return disjointSets.size(); }

  bool empty() const noexcept { return numberOfValues() == 0; }

  ref<const SetType> findGroup(const ValueType &i) const {
    return disjointSets.find(constFind(i))->second;
  }

  ref<const SetType> findGroup(iterator it) const {
    return disjointSets.find(constFind(*it))->second;
  }

  void addValue(const ValueType value) {
    if (internalStorage.find(value) != internalStorage.end()) {
      return;
    }
    parent = parent.insert(value, value);
    roots = roots.insert(value);
    rank = rank.insert(value, 0);
    disjointSets = disjointSets.insert(value, new SetType(value));

    internalStorage = internalStorage.insert(value);
    std::vector<ValueType> oldRoots(roots.begin(), roots.end());
    for (ValueType v : oldRoots) {
      if (!areJoined(v, value) &&
          SetType::intersects(disjointSets.at(find(v)),
                              disjointSets.at(find(value)))) {
        merge(v, value);
      }
    }
  }
  void getAllIndependentSets(std::vector<ref<const SetType>> &result) const {
    for (ValueType v : roots)
      result.push_back(findGroup(v));
  }

  void add(const DisjointSetUnion &b) {
    parent.insert(b.parent.begin(), b.parent.end());
    roots.insert(b.roots.begin(), b.roots.end());
    rank.insert(b.rank.begin(), b.rank.end());

    internalStorage.insert(b.internalStorage.begin(), b.internalStorage.end());
    disjointSets.insert(b.disjointSets.begin(), b.disjointSets.end());
  }
  DisjointSetUnion() {}

  DisjointSetUnion(const internalStorage_ty &is) {
    for (ValueType v : is) {
      addValue(v);
    }
  }

public:
  internalStorage_ty is() const { return internalStorage; }

  disjointSets_ty ds() const { return disjointSets; }
};
} // namespace klee
#endif