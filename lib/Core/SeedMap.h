#ifndef KLEE_SEEDMAP_H
#define KLEE_SEEDMAP_H

#include "ExecutionState.h"
#include "ObjectManager.h"
#include "SeedInfo.h"

#include <map>
#include <set>
namespace klee {

using seeds_ty = std::vector<ExecutingSeed>;

class SeedMap : public Subscriber {
private:
  std::map<ExecutionState *, seeds_ty> seedMap;

public:
  SeedMap();

  void update(ref<ObjectManager::Event> e) override;

  std::map<ExecutionState *, seeds_ty>::iterator
  upper_bound(ExecutionState *state);
  std::map<ExecutionState *, seeds_ty>::iterator find(ExecutionState *state);
  std::map<ExecutionState *, seeds_ty>::iterator end();
  std::map<ExecutionState *, seeds_ty>::iterator begin();
  void erase(std::map<ExecutionState *, seeds_ty>::iterator it);
  void erase(ExecutionState *state);
  void push_back(ExecutionState *state, seeds_ty::iterator siit);
  std::size_t count(ExecutionState *state);
  seeds_ty &at(ExecutionState *state);
  unsigned size() { return seedMap.size(); }
  bool empty();

  virtual ~SeedMap();
};
} // namespace klee

#endif /*KLEE_SEEDMAP_H*/
