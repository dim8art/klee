//===-- Solver.h ------------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_SOLVER_H
#define KLEE_SOLVER_H

#include "klee/Expr/Assignment.h"
#include "klee/Expr/Expr.h"
#include "klee/System/Time.h"
#include "klee/Solver/SolverCmdLine.h"

#include <vector>

namespace klee {
  class ConstraintSet;
  class Expr;
  class SolverImpl;

  /// Collection of meta data that a solver can have access to. This is
  /// independent of the actual constraints but can be used as a two-way
  /// communication between solver and context of query.
  struct SolverQueryMetaData {
    /// @brief Costs for all queries issued for this state
    time::Span queryCost;
  };

  struct Query {
  public:
    const ConstraintSet &constraints;
    ref<Expr> expr;
    bool produceValidityCore = false;

    Query(const ConstraintSet &_constraints, ref<Expr> _expr,
          bool _produceValidityCore = false)
        : constraints(_constraints), expr(_expr),
          produceValidityCore(_produceValidityCore) {}

    Query(const Query &query)
        : constraints(query.constraints), expr(query.expr),
          produceValidityCore(query.produceValidityCore) {}

    /// withExpr - Return a copy of the query with the given expression.
    Query withExpr(ref<Expr> _expr) const {
      return Query(constraints, _expr);
    }

    /// withFalse - Return a copy of the query with a false expression.
    Query withFalse() const {
      return Query(constraints, ConstantExpr::alloc(0, Expr::Bool));
    }

    /// negateExpr - Return a copy of the query with the expression negated.
    Query negateExpr() const {
      return withExpr(Expr::createIsZero(expr));
    }

    Query withValidityCore() const { return Query(constraints, expr, true); }

    Query withConstraints(const ConstraintSet &_constraints) const {
      return Query(_constraints, expr, produceValidityCore);
    }

    /// Dump query
    void dump() const ;
  };

  struct ValidityCore {
  public:
    typedef std::vector<ref<Expr>> constraints_typ;
    std::vector<ref<Expr>> constraints;
    ref<Expr> expr;

    ValidityCore()
        : constraints(std::vector<ref<Expr>>()),
          expr(ConstantExpr::alloc(1, Expr::Bool)) {}

    ValidityCore(const constraints_typ &_constraints, ref<Expr> _expr)
        : constraints(_constraints), expr(_expr) {}

    /// withExpr - Return a copy of the validity core with the given expression.
    ValidityCore withExpr(ref<Expr> _expr) const {
      return ValidityCore(constraints, _expr);
    }

    /// withFalse - Return a copy of the validity core with a false expression.
    ValidityCore withFalse() const {
      return ValidityCore(constraints, ConstantExpr::alloc(0, Expr::Bool));
    }

    /// negateExpr - Return a copy of the validity core with the expression
    /// negated.
    ValidityCore negateExpr() const {
      return withExpr(Expr::createIsZero(expr));
    }

    /// Dump validity core
    void dump() const;

    bool compare(const ValidityCore &b) const {
      return constraints == b.constraints && expr == b.expr;
    }

    bool operator==(const ValidityCore &b) const { return compare(b); }

    bool operator!=(const ValidityCore &b) const { return !compare(b); }
  };

  class SolverResponse {
  public:
    enum ResponseKind {
      Valid = 1,
      Invalid = -1,
    };

    /// @brief Required by klee::ref-managed objects
    class ReferenceCounter _refCount;

    virtual ~SolverResponse() = default;

    virtual ResponseKind getResponseKind() const = 0;

    virtual bool
    getInitialValuesFor(const std::vector<const Array *> &objects,
                        std::vector<std::vector<unsigned char>> &values) {
      return false;
    }

    virtual bool getInitialValues(
        std::map<const Array *, std::vector<unsigned char>> &values) {
      return false;
    }

    virtual bool getValidityCore(ValidityCore &validityCore) { return false; }

    static bool classof(const Query *) { return true; }

    virtual bool compare(const SolverResponse &b) const = 0;

    virtual bool lessThen(const SolverResponse &b) const = 0;

    bool operator==(const SolverResponse &b) const { return compare(b); }

    bool operator!=(const SolverResponse &b) const { return !compare(b); }

    bool operator<(const SolverResponse &b) const { return lessThen(b); }

    virtual void dump() = 0;
  };

  class ValidResponse : public SolverResponse {
  private:
    ValidityCore result;

  public:
    ValidResponse(const ValidityCore &validityCore) : result(validityCore) {}

    bool getValidityCore(ValidityCore &validityCore) {
      validityCore = result;
      return true;
    }

    ResponseKind getResponseKind() const { return Valid; };

    static bool classof(const SolverResponse *result) {
      return result->getResponseKind() == ResponseKind::Valid;
    }
    static bool classof(const ValidResponse *) { return true; }

    bool compare(const SolverResponse &b) const {
      if (b.getResponseKind() != ResponseKind::Valid)
        return false;
      const ValidResponse &vb = static_cast<const ValidResponse &>(b);
      return result == vb.result;
    }

    bool lessThen(const SolverResponse &b) const {
      if (b.getResponseKind() != ResponseKind::Valid)
        return true;
      const ValidResponse &vb = static_cast<const ValidResponse &>(b);
      return result.constraints < vb.result.constraints;
    }

    void dump() {}

  };

  class InvalidResponse : public SolverResponse {
  private:
    Assignment result;

  public:
    InvalidResponse(const std::vector<const Array *> &objects,
                    std::vector<std::vector<unsigned char>> &values)
        : result(Assignment(objects, values)) {}

    InvalidResponse(Assignment::bindings_ty &initialValues)
        : result(initialValues) {}

    bool getInitialValuesFor(const std::vector<const Array *> &objects,
                             std::vector<std::vector<unsigned char>> &values) {
      values.reserve(objects.size());
      for (auto object : objects) {
        if (result.bindings.count(object)) {
          values.push_back(result.bindings.at(object));
        } else {
          return false;
        }
      }
      return true;
    }

    bool getInitialValues(Assignment::bindings_ty &values) {
      values.insert(result.bindings.begin(), result.bindings.end());
      return true;
    }

    ResponseKind getResponseKind() const { return Invalid; };

    static bool classof(const SolverResponse *result) {
      return result->getResponseKind() == ResponseKind::Invalid;
    }
    static bool classof(const InvalidResponse *) { return true; }

    bool compare(const SolverResponse &b) const {
      if (b.getResponseKind() != ResponseKind::Invalid)
        return false;
      const InvalidResponse &ib = static_cast<const InvalidResponse &>(b);
      return result.bindings == ib.result.bindings;
    }

    bool lessThen(const SolverResponse &b) const {
      if (b.getResponseKind() != ResponseKind::Invalid)
        return false;
      const InvalidResponse &ib = static_cast<const InvalidResponse &>(b);
      return result.bindings < ib.result.bindings;
    }

    bool satisfies(std::set<ref<Expr>> &key) {
      return result.satisfies(key.begin(), key.end());
    }

    void dump() { result.dump(); }

    ref<Expr> evaluate(ref<Expr> e) { return (result.evaluate(e)); }
  };

  class Solver {
    // DO NOT IMPLEMENT.
    Solver(const Solver&);
    void operator=(const Solver&);

  public:
    enum Validity {
      True = 1,
      False = -1,
      Unknown = 0
    };
  
  public:
    /// validity_to_str - Return the name of given Validity enum value.
    static const char *validity_to_str(Validity v);

  public:
    SolverImpl *impl;

  public:
    Solver(SolverImpl *_impl) : impl(_impl) {}
    virtual ~Solver();

    /// evaluate - Determine for a particular state if the query
    /// expression is provably true, provably false or neither.
    ///
    /// \param [out] result - if
    /// \f[ \forall X constraints(X) \to query(X) \f]
    /// then Solver::True,
    /// else if
    /// \f[ \forall X constraints(X) \to \lnot query(X) \f]
    /// then Solver::False,
    /// else
    /// Solver::Unknown
    ///
    /// \return True on success.
    bool evaluate(const Query&, Validity &result);
    bool evaluate(const Query &, ref<SolverResponse> &queryResult,
                  ref<SolverResponse> &negateQueryResult);

    /// mustBeTrue - Determine if the expression is provably true.
    /// 
    /// This evaluates the following logical formula:
    ///
    /// \f[ \forall X constraints(X) \to query(X) \f]
    ///
    /// which is equivalent to
    ///
    /// \f[ \lnot \exists X constraints(X) \land \lnot query(X) \f]
    ///
    /// Where \f$X\f$ is some assignment, \f$constraints(X)\f$ are the constraints
    /// in the query and \f$query(X)\f$ is the query expression.
    ///
    /// \param [out] result - On success, true iff the logical formula is true
    ///
    /// \return True on success.
    bool mustBeTrue(const Query&, bool &result);

    /// mustBeFalse - Determine if the expression is provably false.
    ///
    /// This evaluates the following logical formula:
    ///
    /// \f[ \lnot \exists X constraints(X) \land query(X) \f]
    ///
    /// which is equivalent to
    ///
    ///  \f[ \forall X constraints(X) \to \lnot query(X) \f]
    ///
    /// Where \f$X\f$ is some assignment, \f$constraints(X)\f$ are the constraints
    /// in the query and \f$query(X)\f$ is the query expression.
    ///
    /// \param [out] result - On success, true iff the logical formula is false
    ///
    /// \return True on success.
    bool mustBeFalse(const Query&, bool &result);

    /// mayBeTrue - Determine if there is a valid assignment for the given state
    /// in which the expression evaluates to true.
    ///
    /// This evaluates the following logical formula:
    ///
    /// \f[ \exists X constraints(X) \land query(X) \f]
    ///
    /// which is equivalent to
    ///
    /// \f[ \lnot \forall X constraints(X) \to \lnot query(X) \f]
    ///
    /// Where \f$X\f$ is some assignment, \f$constraints(X)\f$ are the constraints
    /// in the query and \f$query(X)\f$ is the query expression.
    ///
    /// \param [out] result - On success, true iff the logical formula may be true
    ///
    /// \return True on success.
    bool mayBeTrue(const Query&, bool &result);

    /// mayBeFalse - Determine if there is a valid assignment for the given
    /// state in which the expression evaluates to false.
    ///
    /// This evaluates the following logical formula:
    ///
    /// \f[ \exists X constraints(X) \land \lnot query(X) \f]
    ///
    /// which is equivalent to
    ///
    /// \f[ \lnot \forall X constraints(X) \to query(X) \f]
    ///
    /// Where \f$X\f$ is some assignment, \f$constraints(X)\f$ are the constraints
    /// in the query and \f$query(X)\f$ is the query expression.
    ///
    /// \param [out] result - On success, true iff the logical formula may be false
    ///
    /// \return True on success.
    bool mayBeFalse(const Query&, bool &result);

    /// getValue - Compute one possible value for the given expression.
    ///
    /// \param [out] result - On success, a value for the expression in some
    /// satisfying assignment.
    ///
    /// \return True on success.
    bool getValue(const Query&, ref<ConstantExpr> &result);

    /// getInitialValues - Compute the initial values for a list of objects.
    ///
    /// \param [out] result - On success, this vector will be filled in with an
    /// array of bytes for each given object (with length matching the object
    /// size). The bytes correspond to the initial values for the objects for
    /// some satisfying assignment.
    ///
    /// \return True on success.
    ///
    /// NOTE: This function returns failure if there is no satisfying
    /// assignment.
    //
    // FIXME: This API is lame. We should probably just provide an API which
    // returns an Assignment object, then clients can get out whatever values
    // they want. This also allows us to optimize the representation.
    bool getInitialValues(const Query&, 
                          const std::vector<const Array*> &objects,
                          std::vector< std::vector<unsigned char> > &result);

    bool getValidityCore(const Query &, ValidityCore &validityCore,
                         bool &result);

    bool check(const Query &, ref<SolverResponse> &queryResult);

    /// getRange - Compute a tight range of possible values for a given
    /// expression.
    ///
    /// \return - A pair with (min, max) values for the expression.
    ///
    /// \post(mustBeTrue(min <= e <= max) && 
    ///       mayBeTrue(min == e) &&
    ///       mayBeTrue(max == e))
    //
    // FIXME: This should go into a helper class, and should handle failure.
    virtual std::pair< ref<Expr>, ref<Expr> > getRange(const Query&);
    
    virtual char *getConstraintLog(const Query& query);
    virtual void setCoreSolverTimeout(time::Span timeout);
  };

  /* *** */

  /// createValidatingSolver - Create a solver which will validate all query
  /// results against an oracle, used for testing that an optimized solver has
  /// the same results as an unoptimized one. This solver will assert on any
  /// mismatches.
  ///
  /// \param s - The primary underlying solver to use.
  /// \param oracle - The solver to check query results against.
  Solver *createValidatingSolver(Solver *s, Solver *oracle);

  /// createAssignmentValidatingSolver - Create a solver that when requested
  /// for an assignment will check that the computed assignment satisfies
  /// the Query.
  /// \param s - The underlying solver to use.
  Solver *createAssignmentValidatingSolver(Solver *s);

  /// createCachingSolver - Create a solver which will cache the queries in
  /// memory (without eviction).
  ///
  /// \param s - The underlying solver to use.
  Solver *createCachingSolver(Solver *s);

  /// createCexCachingSolver - Create a counterexample caching solver. This is a
  /// more sophisticated cache which records counterexamples for a constraint
  /// set and uses subset/superset relations among constraints to try and
  /// quickly find satisfying assignments.
  ///
  /// \param s - The underlying solver to use.
  Solver *createCexCachingSolver(Solver *s);

  /// createFastCexSolver - Create a "fast counterexample solver", which tries
  /// to quickly compute a satisfying assignment for a constraint set using
  /// value propogation and range analysis.
  ///
  /// \param s - The underlying solver to use.
  Solver *createFastCexSolver(Solver *s);

  /// createIndependentSolver - Create a solver which will eliminate any
  /// unnecessary constraints before propogating the query to the underlying
  /// solver.
  ///
  /// \param s - The underlying solver to use.
  Solver *createIndependentSolver(Solver *s);
  
  /// createKQueryLoggingSolver - Create a solver which will forward all queries
  /// after writing them to the given path in .kquery format.
  Solver *createKQueryLoggingSolver(Solver *s, std::string path,
                                    time::Span minQueryTimeToLog,
                                    bool logTimedOut);

  /// createSMTLIBLoggingSolver - Create a solver which will forward all queries
  /// after writing them to the given path in .smt2 format.
  Solver *createSMTLIBLoggingSolver(Solver *s, std::string path,
                                    time::Span minQueryTimeToLog,
                                    bool logTimedOut);


  /// createDummySolver - Create a dummy solver implementation which always
  /// fails.
  Solver *createDummySolver();

  // Create a solver based on the supplied ``CoreSolverType``.
  Solver *createCoreSolver(CoreSolverType cst);
}

#endif /* KLEE_SOLVER_H */
