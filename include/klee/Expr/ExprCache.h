#ifndef KLEE_EXPRCAHCE_H
#define KLEE_EXPRCAHCE_H
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprHashMap.h"

#include <string>
#include <vector>

/// Provides an interface for creating and destroying Expr objects.
class ExprCache {
public:
  ExprCache() {};
	~ExprCache() {};
	static const ref<Expr> CreateExpr(ref<Expr> expression);
private:
	static ExprHashSet cachedExpressions;
};
#endif