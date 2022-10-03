#ifndef KLEE_EXPRCAHCE_H
#define KLEE_EXPRCAHCE_H

#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprHashMap.h"

#include <string>
#include <vector>

namespace klee{
	
	class ExprCache {
		public:
			ExprCache() {};
			~ExprCache() {};
			static ref<Expr> CreateExpr(const ref<Expr> expression);
		private:
			static ExprHashSet cachedExpressions;
	};
}
#endif