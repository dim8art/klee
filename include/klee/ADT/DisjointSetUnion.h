#ifndef KLEE_DISJOINEDSETUNION_H
#define KLEE_DISJOINEDSETUNION_H
#include "klee/ADT/Ref.h"

#include <set>
#include <unordered_set>
#include <vector>

namespace klee {

template <typename ValueType, typename SetType> class DisjointSetUnion {
protected:
  size_t capacity = 0;
  std::vector<size_t> parent;
  std::unordered_set<size_t> roots;
  std::vector<size_t> rank;

  std::vector<ValueType> internalStorage;
  std::vector<ref<SetType>> disjointSets;

  size_t find(const size_t &v) {
    assert(v < capacity && v >= 0);
    if (v == parent[v])
      return v;
    return parent[v] = find(parent[v]);
  }

  size_t constFind(const size_t &v) const {
    assert(v < capacity && v >= 0);
    if (v == parent[v])
      return v;
    return constFind(parent[v]);
  }

  void merge(size_t a, size_t b) {
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

  bool joined(const size_t &i, const size_t &j) const {
    return constFind(i) == constFind(j);
  }

public:
  typedef std::vector<ValueType> internalStorage_ty;
  typedef typename internalStorage_ty::const_iterator const_iterator;

  bool empty() const;
  const_iterator begin() const { return internalStorage.begin(); }
  const_iterator end() const { return internalStorage.end(); }
  size_t size() const noexcept { return capacity; }

  ValueType load(const size_t &i) const { return internalStorage[i]; }
  ValueType operator[](const size_t &i) const { return load(i); }

  ref<SetType> findGroup(const size_t &i) const {
    return disjointSets[constFind(i)];
  }
  ref<SetType> findGroup(const_iterator it) const {
    return disjointSets[constFind(it - internalStorage.begin())];
  }

  void addValue(const ValueType value) {
    capacity++;
    parent.push_back(capacity - 1);
    roots.insert(capacity - 1);
    rank.push_back(0);

    internalStorage.push_back(value);
    disjointSets.push_back(new SetType(value));
    std::vector<size_t> oldRoots(roots.begin(), roots.end());
    for (size_t r : oldRoots) {
      if (!joined(r, capacity - 1) &&
          SetType::intersects(disjointSets[find(r)],
                              disjointSets[find(capacity - 1)])) {
        merge(r, capacity - 1);
      }
    }
  }

  void getAllIndependentSets(std::vector<ref<SetType>> &result) const {
    for (size_t r : roots) {
      result.push_back(disjointSets[r]);
    }
  }

  DisjointSetUnion(const internalStorage_ty &is) {
    for (ValueType v : is) {
      addValue(v);
    }
  }
};
} // namespace klee
#endif