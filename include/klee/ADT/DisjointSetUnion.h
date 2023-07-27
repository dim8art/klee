#ifndef KLEE_DISJOINEDSETUNION_H
#define KLEE_DISJOINEDSETUNION_H
#include "klee/ADT/Ref.h"

#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace klee {

template <typename ValueType, typename SetType, typename HashType>
class DisjointSetUnion {
protected:
  std::unordered_map<ValueType, ValueType, HashType> parent;
  std::unordered_set<ValueType, HashType> roots;
  std::unordered_map<ValueType, size_t, HashType> rank;

  std::unordered_set<ValueType, HashType> internalStorage;
  std::unordered_map<ValueType, ref<const SetType>, HashType> disjointSets;

  ValueType find(const ValueType &v) { // findparent
    assert(parent.find(v) != parent.end());
    if (v == parent[v])
      return v;
    return parent[v] = find(parent[v]);
  }

  ValueType constFind(const ValueType &v) const {
    assert(parent.find(v) != parent.end());
    ValueType v1 = parent.find(v)->second;
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

    if (rank[a] < rank[b]) {
      std::swap(a, b);
    }
    parent[b] = a;
    if (rank[a] == rank[b]) {
      rank[a]++;
    }

    roots.erase(b);
    disjointSets[a] = SetType::merge(disjointSets[a], disjointSets[b]);
    disjointSets[b] = nullptr;
  }

  bool areJoined(const ValueType &i, const ValueType &j) const {
    return constFind(i) == constFind(j);
  }

public:
  using internalStorage_ty = std::unordered_set<ValueType, HashType>;
  using disjointSets_ty =
      std::unordered_map<ValueType, ref<const SetType>, HashType>;
  using const_iterator = typename internalStorage_ty::const_iterator;

  const_iterator begin() const { return internalStorage.begin(); }
  const_iterator end() const { return internalStorage.end(); }

  size_t numberOfValues() const noexcept { return internalStorage.size(); }

  size_t numberOfGroups() const noexcept { return disjointSets.size(); }

  bool empty() const noexcept { return numberOfValues() == 0; }

  ref<const SetType> findGroup(const ValueType &i) const {
    return disjointSets.find(constFind(i))->second;
  }

  ref<const SetType> findGroup(const_iterator it) const {
    return disjointSets.find(constFind(*it))->second;
  }

  void addValue(const ValueType value) {
    if (internalStorage.find(value) != internalStorage.end()) {
      return;
    }
    parent[value] = value;
    roots.insert(value);
    rank[value] = 0;
    disjointSets[value] = new SetType(value);

    internalStorage.insert(value);
    std::vector<ValueType> oldRoots(roots.begin(), roots.end());
    for (ValueType v : oldRoots) {
      if (!areJoined(v, value) &&
          SetType::intersects(disjointSets[find(v)],
                              disjointSets[find(value)])) {
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