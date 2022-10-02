#include "klee/Expr/ExprCache.h"

namespace klee {

ExprCache::~ExprCache(){
	// Free ExprHashSet object
  for (ExprHashSet::iterator ai = cachedExpressions.begin(),
														 e = cachedExpressions.end(),
			ai != e; ++ai) {
				delete *ai;
			}
}

static const ref<Expr> 
ExprCache::CreateExpr(ref<Expr> expression){
	std::pair<ExprHashSet::const_iterator, bool> success =
			cachedExpressions.insert(expression);
	if (success.second) {
		// Cache miss
		return expression;
	}
	//Cache hit

	return *(success.first);
}