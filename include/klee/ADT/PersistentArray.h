#ifndef KLEE_PERSISTENTARRAY_H
#define KLEE_PERSISTENTARRAY_H

#include <cassert>
#include <cstddef>
#include <iterator>
#include <list>
#include <memory>
#include <set>
#include <vector>

#include "OrderMaintenanceTrie.h"

namespace klee {

template <typename ValueType> class PersistentArray {
public:
  struct Modification {
    ValueType value;
    OrderMaintenanaceTrie::Iterator listPosition;
    Modification(ValueType v, OrderMaintenanaceTrie::Iterator lp =
                                  OrderMaintenanaceTrie::Iterator())
        : value(v), listPosition(lp) {}
  };

  typedef std::list<Modification> VersionList;

  struct Sublist {
    typename VersionList::const_iterator initialElement;
    typename VersionList::const_iterator modifiedElement;
    Sublist(typename VersionList::const_iterator iE,
            typename VersionList::const_iterator mE) {
      initialElement = iE;
      modifiedElement = mE;
    }
  };

  struct positionCmp {
    bool operator()(const Sublist &a, const Sublist &b) const {
      return *(a.initialElement->listPosition) <
             *(b.initialElement->listPosition);
    }
  };
  typedef std::set<Sublist, positionCmp> SublistSet;

private:
  std::shared_ptr<VersionList> versionList;
  std::shared_ptr<OrderMaintenanaceTrie> versionListOrder;
  std::shared_ptr<std::vector<SublistSet>> sublists;
  typename VersionList::const_iterator version;

  size_t capacity;

public:
  const typename VersionList::const_iterator getVersion() { return version; }
  const std::shared_ptr<VersionList> getVersionList() { return versionList; }
  const std::shared_ptr<std::vector<SublistSet>> getSublists() {
    return sublists;
  }
  size_t size() const { return capacity; }

  PersistentArray<ValueType>() {
    versionList = std::shared_ptr<VersionList>(new VersionList);
    versionListOrder =
        std::shared_ptr<OrderMaintenanaceTrie>(new OrderMaintenanaceTrie);
    sublists =
        std::shared_ptr<std::vector<SublistSet>>(new std::vector<SublistSet>());
    versionList->push_front(
        Modification(ValueType(), versionListOrder->begin()));
    version = versionList->begin();
    capacity = 0;
  }

  PersistentArray(size_t n) {
    versionList = new VersionList;
    sublists = new std::vector<SublistSet>(n);
    versionList->push_front(
        Modification(ValueType(), versionListOrder->begin()));
    version = versionList->begin();
    for (auto &i : *sublists) {
      i.insert(Sublist(version, version));
    }
    capacity = n;
  }

  static PersistentArray<ValueType>
  storeInVersion(PersistentArray<ValueType> &array, const size_t &i,
                 const ValueType &b) {
    // insert modification to versionList
    assert(i < array.size() && i >= 0);
    PersistentArray<ValueType> newArray(array);
    typename VersionList::const_iterator v = newArray.version;

    Modification mod(b);
    mod.listPosition =
        newArray.versionListOrder->insertAfter(array.version->listPosition);
    newArray.version = newArray.versionList->insert(++v, mod);

    // insert sublists created with modification to sublists array

    SublistSet &sI = (*newArray.sublists)[i];
    Sublist SLE(newArray.version, newArray.version);
    typename SublistSet::iterator lowerBound = sI.lower_bound(SLE);

    typename VersionList::const_iterator nextElement = newArray.version;
    nextElement++;

    if (lowerBound != sI.begin() &&
        nextElement != newArray.versionList->end()) {
      Sublist prevSLE = *(--lowerBound);
      sI.insert(Sublist(nextElement, prevSLE.modifiedElement));
    }
    sI.insert(SLE);
    return newArray;
  }

  ValueType loadFromVersion(PersistentArray<ValueType> &array,
                            const size_t &i) {
    Sublist sample(array.version, array.version);
    SublistSet &sI = (*array.sublists)[i];

    typename SublistSet::iterator result = sI.upper_bound(sample);

    if (result == sI.begin()) {
      return ValueType();
    }

    return (--result)->modifiedElement->value;
  }

  PersistentArray<ValueType> store(const size_t &i, const ValueType &b) {
    return storeInVersion(*this, i, b);
  }

  static PersistentArray<ValueType>
  pushBackInVersion(PersistentArray<ValueType> &array, const ValueType &b) {
    array.sublists->push_back(
        {Sublist(array.versionList->begin(), array.versionList->begin())});
    PersistentArray<ValueType> newArray(array);
    newArray.capacity++;
    newArray = newArray.store(newArray.capacity - 1, b);
    return newArray;
  }

  PersistentArray<ValueType> push_back(const ValueType &b) {
    return pushBackInVersion(*this, b);
  }

  ValueType load(const size_t &i) { return loadFromVersion(*this, i); }

  ValueType operator[](const size_t &i) { return load(i); }

  bool operator==(const PersistentArray<ValueType> &b) const {
    if (this->size() != b.size())
      return false;
    for (int i = 0; i < this->size(); i++) {
      if (this[i] != b[i])
        return false;
    }
    return true;
  }

  bool operator!=(const PersistentArray<ValueType> &b) const {
    return !(*this == b);
  }

  bool operator<(const PersistentArray<ValueType> &b) const {
    std::vector<ValueType> aInternal;
    std::vector<ValueType> bInternal;
    for (int i = 0; i < this->size(); i++) {
      aInternal.push_back(this[i]);
    }
    for (int i = 0; i < b.size(); i++) {
      bInternal.push_back(b[i]);
    }
    return aInternal < bInternal;
  }

  bool operator>(const PersistentArray<ValueType> &b) const {
    std::vector<ValueType> aInternal;
    std::vector<ValueType> bInternal;
    for (int i = 0; i < this->size(); i++) {
      aInternal.push_back(this[i]);
    }
    for (int i = 0; i < b.size(); i++) {
      bInternal.push_back(b[i]);
    }
    return aInternal > bInternal;
  }
};
} // namespace klee
#endif
