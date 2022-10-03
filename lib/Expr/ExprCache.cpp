#include "klee/Expr/ExprCache.h"

namespace klee{

	ref<Expr> 
	ExprCache::CreateExpr(const ref<Expr> expression){
		std::pair<ExprHashSet::const_iterator, bool> success =
				cachedExpressions.insert(expression);
		if (success.second) {
			// Cache miss
			return expression;
		}
		//Cache hit

		return *(success.first);
	}
}