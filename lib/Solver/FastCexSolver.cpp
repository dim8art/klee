//===-- FastCexSolver.cpp -------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "cex-solver"
#include "klee/Solver/Solver.h"

#include "klee/Expr/Constraints.h"
#include "klee/Expr/Expr.h"
#include "klee/Expr/ExprEvaluator.h"
#include "klee/Expr/ExprRangeEvaluator.h"
#include "klee/Expr/ExprVisitor.h"
#include "klee/Solver/IncompleteSolver.h"
#include "klee/Support/Debug.h"
#include "klee/Support/ErrorHandling.h"

#include "klee/Support/CompilerWarning.h"
DISABLE_WARNING_PUSH
DISABLE_WARNING_DEPRECATED_DECLARATIONS
#include "llvm/ADT/APInt.h"
#include "llvm/Support/raw_ostream.h"
DISABLE_WARNING_POP

#include <cassert>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

using namespace klee;

// Hacker's Delight, pgs 58-63
static uint64_t minOR(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  uint64_t temp, m = ((uint64_t)1) << 63;
  while (m) {
    if (~a & c & m) {
      temp = (a | m) & -m;
      if (temp <= b) {
        a = temp;
        break;
      }
    } else if (a & ~c & m) {
      temp = (c | m) & -m;
      if (temp <= d) {
        c = temp;
        break;
      }
    }
    m >>= 1;
  }

  return a | c;
}
static uint64_t maxOR(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  uint64_t temp, m = ((uint64_t)1) << 63;

  while (m) {
    if (b & d & m) {
      temp = (b - m) | (m - 1);
      if (temp >= a) {
        b = temp;
        break;
      }
      temp = (d - m) | (m - 1);
      if (temp >= c) {
        d = temp;
        break;
      }
    }
    m >>= 1;
  }

  return b | d;
}
static uint64_t minAND(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  uint64_t temp, m = ((uint64_t)1) << 63;
  while (m) {
    if (~a & ~c & m) {
      temp = (a | m) & -m;
      if (temp <= b) {
        a = temp;
        break;
      }
      temp = (c | m) & -m;
      if (temp <= d) {
        c = temp;
        break;
      }
    }
    m >>= 1;
  }

  return a & c;
}
static uint64_t maxAND(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
  uint64_t temp, m = ((uint64_t)1) << 63;
  while (m) {
    if (b & ~d & m) {
      temp = (b & ~m) | (m - 1);
      if (temp >= a) {
        b = temp;
        break;
      }
    } else if (~b & d & m) {
      temp = (d & ~m) | (m - 1);
      if (temp >= c) {
        d = temp;
        break;
      }
    }
    m >>= 1;
  }

  return b & d;
}

///

class ValueRange {
private:
  std::uint64_t m_min = 1, m_max = 0;

public:
  ValueRange() noexcept = default;
  ValueRange(const ref<ConstantExpr> &ce) {
    // FIXME: Support large widths.
    m_min = m_max = ce->getLimitedValue();
  }
  explicit ValueRange(std::uint64_t value) noexcept
      : m_min(value), m_max(value) {}
  ValueRange(std::uint64_t _min, std::uint64_t _max) noexcept
      : m_min(_min), m_max(_max) {}
  ValueRange(const ValueRange &other) noexcept = default;
  ValueRange &operator=(const ValueRange &other) noexcept = default;
  ValueRange(ValueRange &&other) noexcept = default;
  ValueRange &operator=(ValueRange &&other) noexcept = default;

  void print(llvm::raw_ostream &os) const {
    if (isFixed()) {
      os << m_min;
    } else {
      os << "[" << m_min << "," << m_max << "]";
    }
  }

  bool isEmpty() const noexcept { return m_min > m_max; }
  bool contains(std::uint64_t value) const {
    return this->intersects(ValueRange(value));
  }
  bool intersects(const ValueRange &b) const {
    return !this->set_intersection(b).isEmpty();
  }

  bool isFullRange(unsigned bits) const noexcept {
    return m_min == 0 && m_max == bits64::maxValueOfNBits(bits);
  }

  ValueRange set_intersection(const ValueRange &b) const {
    return ValueRange(std::max(m_min, b.m_min), std::min(m_max, b.m_max));
  }
  ValueRange set_union(const ValueRange &b) const {
    return ValueRange(std::min(m_min, b.m_min), std::max(m_max, b.m_max));
  }
  ValueRange set_difference(const ValueRange &b) const {
    if (b.isEmpty() || b.m_min > m_max || b.m_max < m_min) { // no intersection
      return *this;
    } else if (b.m_min <= m_min && b.m_max >= m_max) { // empty
      return ValueRange(1, 0);
    } else if (b.m_min <= m_min) { // one range out
      // cannot overflow because b.m_max < m_max
      return ValueRange(b.m_max + 1, m_max);
    } else if (b.m_max >= m_max) {
      // cannot overflow because b.min > m_min
      return ValueRange(m_min, b.m_min - 1);
    } else {
      // two ranges, take bottom
      return ValueRange(m_min, b.m_min - 1);
    }
  }
  ValueRange binaryAnd(const ValueRange &b) const {
    // XXX
    assert(!isEmpty() && !b.isEmpty() && "XXX");
    if (isFixed() && b.isFixed()) {
      return ValueRange(m_min & b.m_min);
    } else {
      return ValueRange(minAND(m_min, m_max, b.m_min, b.m_max),
                        maxAND(m_min, m_max, b.m_min, b.m_max));
    }
  }
  ValueRange binaryAnd(std::uint64_t b) const {
    return binaryAnd(ValueRange(b));
  }
  ValueRange binaryOr(ValueRange b) const {
    // XXX
    assert(!isEmpty() && !b.isEmpty() && "XXX");
    if (isFixed() && b.isFixed()) {
      return ValueRange(m_min | b.m_min);
    } else {
      return ValueRange(minOR(m_min, m_max, b.m_min, b.m_max),
                        maxOR(m_min, m_max, b.m_min, b.m_max));
    }
  }
  ValueRange binaryOr(std::uint64_t b) const { return binaryOr(ValueRange(b)); }
  ValueRange binaryXor(ValueRange b) const {
    if (isFixed() && b.isFixed()) {
      return ValueRange(m_min ^ b.m_min);
    } else {
      std::uint64_t t = m_max | b.m_max;
      while (!bits64::isPowerOfTwo(t))
        t = bits64::withoutRightmostBit(t);
      return ValueRange(0, (t << 1) - 1);
    }
  }

  ValueRange binaryShiftLeft(unsigned bits) const {
    return ValueRange(m_min << bits, m_max << bits);
  }
  ValueRange binaryShiftRight(unsigned bits) const {
    return ValueRange(m_min >> bits, m_max >> bits);
  }

  ValueRange concat(const ValueRange &b, unsigned bits) const {
    return binaryShiftLeft(bits).binaryOr(b);
  }
  ValueRange extract(std::uint64_t lowBit, std::uint64_t maxBit) const {
    return binaryShiftRight(lowBit).binaryAnd(
        bits64::maxValueOfNBits(maxBit - lowBit));
  }

  ValueRange add(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }
  ValueRange sub(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }
  ValueRange mul(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }
  ValueRange udiv(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }
  ValueRange sdiv(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }
  ValueRange urem(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }
  ValueRange srem(const ValueRange &b, unsigned width) const {
    return ValueRange(0, bits64::maxValueOfNBits(width));
  }

  // use min() to get value if true (XXX should we add a method to
  // make code clearer?)
  bool isFixed() const noexcept { return m_min == m_max; }

  bool operator==(const ValueRange &b) const noexcept {
    return m_min == b.m_min && m_max == b.m_max;
  }
  bool operator!=(const ValueRange &b) const noexcept { return !(*this == b); }

  bool mustEqual(const std::uint64_t b) const noexcept {
    return m_min == m_max && m_min == b;
  }
  bool mayEqual(const std::uint64_t b) const noexcept {
    return m_min <= b && m_max >= b;
  }

  bool mustEqual(const ValueRange &b) const noexcept {
    return isFixed() && b.isFixed() && m_min == b.m_min;
  }
  bool mayEqual(const ValueRange &b) const { return this->intersects(b); }

  std::uint64_t min() const noexcept {
    assert(!isEmpty() && "cannot get minimum of empty range");
    return m_min;
  }

  std::uint64_t max() const noexcept {
    assert(!isEmpty() && "cannot get maximum of empty range");
    return m_max;
  }

  std::int64_t minSigned(unsigned bits) const {
    assert(bits >= 2 && bits <= 64);
    assert((m_min >> bits) == 0 && (m_max >> bits) == 0 &&
           "range is outside given number of bits");

    // if max allows sign bit to be set then it can be smallest value,
    // otherwise since the range is not empty, min cannot have a sign
    // bit

    std::uint64_t smallest = (static_cast<std::uint64_t>(1) << (bits - 1));
    if (m_max >= smallest) {
      return llvm::APInt::getSignedMinValue(bits).getSExtValue();
    } else {
      return m_min;
    }
  }

  std::int64_t maxSigned(unsigned bits) const {
    assert(bits >= 2 && bits <= 64);
    assert((m_min >> bits) == 0 && (m_max >> bits) == 0 &&
           "range is outside given number of bits");

    std::uint64_t smallest = (static_cast<std::uint64_t>(1) << (bits - 1));

    // if max and min have sign bit then max is max, otherwise if only
    // max has sign bit then max is largest signed integer, otherwise
    // max is max

    if (m_min < smallest && m_max >= smallest) {
      return smallest - 1;
    } else {
      return llvm::APInt(bits, m_max, true).getSExtValue();
    }
  }
};

inline llvm::raw_ostream &operator<<(llvm::raw_ostream &os,
                                     const ValueRange &vr) {
  vr.print(os);
  return os;
}

// XXX waste of space, rather have ByteValueRange
typedef ValueRange CexValueData;

class CexObjectData {
  /// possibleContents - An array of "possible" values for the object.
  ///
  /// The possible values is an inexact approximation for the set of values for
  /// each array location.
  std::vector<CexValueData> possibleContents;

  /// exactContents - An array of exact values for the object.
  ///
  /// The exact values are a conservative approximation for the set of values
  /// for each array location.
  std::vector<CexValueData> exactContents;

  CexObjectData(const CexObjectData &);  // DO NOT IMPLEMENT
  void operator=(const CexObjectData &); // DO NOT IMPLEMENT

public:
  CexObjectData(uint64_t size) : possibleContents(size), exactContents(size) {
    for (uint64_t i = 0; i != size; ++i) {
      possibleContents[i] = ValueRange(0, 255);
      exactContents[i] = ValueRange(0, 255);
    }
  }

  const CexValueData getPossibleValues(size_t index) const {
    return possibleContents[index];
  }
  void setPossibleValues(size_t index, CexValueData values) {
    possibleContents[index] = values;
  }
  void setPossibleValue(size_t index, unsigned char value) {
    possibleContents[index] = CexValueData(value);
  }

  const CexValueData getExactValues(size_t index) const {
    return exactContents[index];
  }
  void setExactValues(size_t index, CexValueData values) {
    exactContents[index] = values;
  }

  /// getPossibleValue - Return some possible value.
  unsigned char getPossibleValue(size_t index) const {
    const CexValueData &cvd = possibleContents[index];
    return cvd.min() + (cvd.max() - cvd.min()) / 2;
  }
};

class CexRangeEvaluator : public ExprRangeEvaluator<ValueRange> {
public:
  std::map<const Array *, CexObjectData *> &objects;
  CexRangeEvaluator(std::map<const Array *, CexObjectData *> &_objects)
      : objects(_objects) {}

  ValueRange getInitialReadRange(const Array &array, ValueRange index) {
    // Check for a concrete read of a constant array.
    if (array.isConstantArray() && index.isFixed()) {
      if (ref<ConstantSource> constantSource =
              dyn_cast<ConstantSource>(array.source)) {
        if (index.min() < constantSource->constantValues.size()) {
          return ValueRange(
              constantSource->constantValues[index.min()]->getZExtValue(8));
        }
      } else if (ref<SymbolicSizeConstantSource> symbolicSizeConstantSource =
                     dyn_cast<SymbolicSizeConstantSource>(array.source)) {
        return ValueRange(symbolicSizeConstantSource->defaultValue);
      }
    }
    return ValueRange(0, 255);
  }
};

class CexPossibleEvaluator : public ExprEvaluator {
protected:
  ref<Expr> getInitialValue(const Array &array, unsigned index) {
    // If the index is out of range, we cannot assign it a value, since that
    // value cannot be part of the assignment.
    ref<ConstantExpr> constantArraySize = dyn_cast<ConstantExpr>(array.size);
    if (!constantArraySize) {
      klee_error(
          "FIXME: Arrays of symbolic sizes are unsupported in FastCex\n");
      std::abort();
    }

    if (index >= constantArraySize->getZExtValue()) {
      return ReadExpr::create(UpdateList(&array, 0),
                              ConstantExpr::alloc(index, array.getDomain()));
    }

    std::map<const Array *, CexObjectData *>::iterator it =
        objects.find(&array);
    return ConstantExpr::alloc(
        (it == objects.end() ? 127 : it->second->getPossibleValue(index)),
        array.getRange());
  }

public:
  std::map<const Array *, CexObjectData *> &objects;
  CexPossibleEvaluator(std::map<const Array *, CexObjectData *> &_objects)
      : objects(_objects) {}
};

class CexExactEvaluator : public ExprEvaluator {
protected:
  ref<Expr> getInitialValue(const Array &array, unsigned index) {
    // If the index is out of range, we cannot assign it a value, since that
    // value cannot be part of the assignment.
    ref<ConstantExpr> constantArraySize = dyn_cast<ConstantExpr>(array.size);
    if (!constantArraySize) {
      klee_error(
          "FIXME: Arrays of symbolic sizes are unsupported in FastCex\n");
      std::abort();
    }

    if (index >= constantArraySize->getZExtValue()) {
      return ReadExpr::create(UpdateList(&array, 0),
                              ConstantExpr::alloc(index, array.getDomain()));
    }

    std::map<const Array *, CexObjectData *>::iterator it =
        objects.find(&array);
    if (it == objects.end())
      return ReadExpr::create(UpdateList(&array, 0),
                              ConstantExpr::alloc(index, array.getDomain()));

    CexValueData cvd = it->second->getExactValues(index);
    if (!cvd.isFixed())
      return ReadExpr::create(UpdateList(&array, 0),
                              ConstantExpr::alloc(index, array.getDomain()));

    return ConstantExpr::alloc(cvd.min(), array.getRange());
  }

public:
  std::map<const Array *, CexObjectData *> &objects;
  CexExactEvaluator(std::map<const Array *, CexObjectData *> &_objects)
      : objects(_objects) {}
};

class CexData {
public:
  std::map<const Array *, CexObjectData *> objects;

  CexData(const CexData &);        // DO NOT IMPLEMENT
  void operator=(const CexData &); // DO NOT IMPLEMENT

public:
  CexData() {}
  ~CexData() {
    for (std::map<const Array *, CexObjectData *>::iterator
             it = objects.begin(),
             ie = objects.end();
         it != ie; ++it)
      delete it->second;
  }

  CexObjectData &getObjectData(const Array *A) {
    CexObjectData *&Entry = objects[A];

    ref<ConstantExpr> constantArraySize = dyn_cast<ConstantExpr>(A->size);
    if (!constantArraySize) {
      klee_error(
          "FIXME: Arrays of symbolic sizes are unsupported in FastCex\n");
      std::abort();
    }

    if (!Entry)
      Entry = new CexObjectData(constantArraySize->getZExtValue());

    return *Entry;
  }

  void propagatePossibleValue(ref<Expr> e, uint64_t value) {
    propagatePossibleValues(e, CexValueData(value, value));
  }

  void propagateExactValue(ref<Expr> e, uint64_t value) {
    propagateExactValues(e, CexValueData(value, value));
  }

  void propagatePossibleValues(ref<Expr> e, CexValueData range) {
    KLEE_DEBUG(llvm::errs() << "propagate: " << range << " for\n" << e << "\n");

    switch (e->getKind()) {
    case Expr::Constant:
      // rather a pity if the constant isn't in the range, but how can
      // we use this?
      break;

      // Special

    case Expr::NotOptimized:
      break;

    case Expr::Read: {
      ReadExpr *re = cast<ReadExpr>(e);
      const Array *array = re->updates.root;
      CexObjectData &cod = getObjectData(array);

      // FIXME: This is imprecise, we need to look through the existing writes
      // to see if this is an initial read or not.
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
        if (ref<ConstantExpr> constantArraySize =
                dyn_cast<ConstantExpr>(array->size)) {
          uint64_t index = CE->getZExtValue();

          if (index < constantArraySize->getZExtValue()) {
            // If the range is fixed, just set that; even if it conflicts with
            // the previous range it should be a better guess.
            if (range.isFixed()) {
              cod.setPossibleValue(index, range.min());
            } else {
              CexValueData cvd = cod.getPossibleValues(index);
              CexValueData tmp = cvd.set_intersection(range);

              if (!tmp.isEmpty())
                cod.setPossibleValues(index, tmp);
            }
          }
        } else {
          // XXX        fatal("XXX not implemented");
        }
      } else {
        // XXX        fatal("XXX not implemented");
      }
      break;
    }

    case Expr::Select: {
      SelectExpr *se = cast<SelectExpr>(e);
      ValueRange cond = evalRangeForExpr(se->cond);
      if (cond.isFixed()) {
        if (cond.min()) {
          propagatePossibleValues(se->trueExpr, range);
        } else {
          propagatePossibleValues(se->falseExpr, range);
        }
      } else {
        // XXX imprecise... we have a choice here. One method is to
        // simply force both sides into the specified range (since the
        // condition is indetermined). This may lose in two ways, the
        // first is that the condition chosen may limit further
        // restrict the range in each of the children, however this is
        // less of a problem as the range will be a superset of legal
        // values. The other is if the condition ends up being forced
        // by some other constraints, then we needlessly forced one
        // side into the given range.
        //
        // The other method would be to force the condition to one
        // side and force that side into the given range. This loses
        // when we force the condition to an unsatisfiable value
        // (either because the condition cannot be that, or the
        // resulting range given that condition is not in the required
        // range).
        //
        // Currently we just force both into the range. A hybrid would
        // be to evaluate the ranges for each of the children... if
        // one of the ranges happens to already be a subset of the
        // required range then it may be preferable to force the
        // condition to that side.
        propagatePossibleValues(se->trueExpr, range);
        propagatePossibleValues(se->falseExpr, range);
      }
      break;
    }

      // XXX imprecise... the problem here is that extracting bits
      // loses information about what bits are connected across the
      // bytes. if a value can be 1 or 256 then either the top or
      // lower byte is 0, but just extraction loses this information
      // and will allow neither,one,or both to be 1.
      //
      // we can protect against this in a limited fashion by writing
      // the extraction a byte at a time, then checking the evaluated
      // value, isolating for that range, and continuing.
    case Expr::Concat: {
      ConcatExpr *ce = cast<ConcatExpr>(e);
      Expr::Width LSBWidth = ce->getKid(1)->getWidth();
      Expr::Width MSBWidth = ce->getKid(1)->getWidth();
      propagatePossibleValues(ce->getKid(0),
                              range.extract(LSBWidth, LSBWidth + MSBWidth));
      propagatePossibleValues(ce->getKid(1), range.extract(0, LSBWidth));
      break;
    }

    case Expr::Extract: {
      // XXX
      break;
    }

      // Casting

      // Simply intersect the output range with the range of all possible
      // outputs and then truncate to the desired number of bits.

      // For ZExt this simplifies to just intersection with the possible input
      // range.
    case Expr::ZExt: {
      CastExpr *ce = cast<CastExpr>(e);
      unsigned inBits = ce->src->getWidth();
      ValueRange input = range.set_intersection(
          ValueRange(0, bits64::maxValueOfNBits(inBits)));
      propagatePossibleValues(ce->src, input);
      break;
    }
      // For SExt instead of doing the intersection we just take the output
      // range minus the impossible values. This is nicer since it is a single
      // interval.
    case Expr::SExt: {
      CastExpr *ce = cast<CastExpr>(e);
      unsigned inBits = ce->src->getWidth();
      unsigned outBits = ce->width;
      ValueRange output = range.set_difference(ValueRange(
          1 << (inBits - 1), (bits64::maxValueOfNBits(outBits) -
                              bits64::maxValueOfNBits(inBits - 1) - 1)));
      ValueRange input = output.binaryAnd(bits64::maxValueOfNBits(inBits));
      propagatePossibleValues(ce->src, input);
      break;
    }

      // Binary

    case Expr::Add: {
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left)) {
        // FIXME: Don't depend on width.
        if (CE->getWidth() <= 64) {
          // FIXME: Why do we ever propagate empty ranges? It doesn't make
          // sense.
          if (range.isEmpty())
            break;

          // C_0 + X \in [MIN, MAX) ==> X \in [MIN - C_0, MAX - C_0)
          Expr::Width W = CE->getWidth();
          CexValueData nrange(
              ConstantExpr::alloc(range.min(), W)->Sub(CE)->getZExtValue(),
              ConstantExpr::alloc(range.max(), W)->Sub(CE)->getZExtValue());
          if (!nrange.isEmpty())
            propagatePossibleValues(be->right, nrange);
        }
      }
      break;
    }

    case Expr::And: {
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (be->getWidth() == Expr::Bool) {
        if (range.isFixed()) {
          ValueRange left = evalRangeForExpr(be->left);
          ValueRange right = evalRangeForExpr(be->right);

          if (!range.min()) {
            if (left.mustEqual(0) || right.mustEqual(0)) {
              // all is well
            } else {
              // XXX heuristic, which order

              propagatePossibleValue(be->left, 0);
              left = evalRangeForExpr(be->left);

              // see if that worked
              if (!left.mustEqual(1))
                propagatePossibleValue(be->right, 0);
            }
          } else {
            if (!left.mustEqual(1))
              propagatePossibleValue(be->left, 1);
            if (!right.mustEqual(1))
              propagatePossibleValue(be->right, 1);
          }
        }
      } else {
        // XXX
      }
      break;
    }

    case Expr::Or: {
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (be->getWidth() == Expr::Bool) {
        if (range.isFixed()) {
          ValueRange left = evalRangeForExpr(be->left);
          ValueRange right = evalRangeForExpr(be->right);

          if (range.min()) {
            if (left.mustEqual(1) || right.mustEqual(1)) {
              // all is well
            } else {
              // XXX heuristic, which order?

              // force left to value we need
              propagatePossibleValue(be->left, 1);
              left = evalRangeForExpr(be->left);

              // see if that worked
              if (!left.mustEqual(1))
                propagatePossibleValue(be->right, 1);
            }
          } else {
            if (!left.mustEqual(0))
              propagatePossibleValue(be->left, 0);
            if (!right.mustEqual(0))
              propagatePossibleValue(be->right, 0);
          }
        }
      } else {
        // XXX
      }
      break;
    }

    case Expr::Xor:
      break;

      // Comparison

    case Expr::Eq: {
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (range.isFixed()) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left)) {
          // FIXME: Handle large widths?
          if (CE->getWidth() <= 64) {
            uint64_t value = CE->getZExtValue();
            if (range.min()) {
              propagatePossibleValue(be->right, value);
            } else {
              CexValueData range;
              if (value == 0) {
                range =
                    CexValueData(1, bits64::maxValueOfNBits(CE->getWidth()));
              } else {
                // FIXME: heuristic / lossy, could be better to pick larger
                // range?
                range = CexValueData(0, value - 1);
              }
              propagatePossibleValues(be->right, range);
            }
          } else {
            // XXX what now
          }
        }
      }
      break;
    }

    case Expr::Not: {
      if (e->getWidth() == Expr::Bool && range.isFixed()) {
        propagatePossibleValue(e->getKid(0), !range.min());
      }
      break;
    }

    case Expr::Ult: {
      BinaryExpr *be = cast<BinaryExpr>(e);

      // XXX heuristic / lossy, what order if conflict

      if (range.isFixed()) {
        ValueRange left = evalRangeForExpr(be->left);
        ValueRange right = evalRangeForExpr(be->right);

        uint64_t maxValue = bits64::maxValueOfNBits(be->right->getWidth());

        // XXX should deal with overflow (can lead to empty range)

        if (left.isFixed()) {
          if (range.min()) {
            propagatePossibleValues(be->right,
                                    CexValueData(left.min() + 1, maxValue));
          } else {
            propagatePossibleValues(be->right, CexValueData(0, left.min()));
          }
        } else if (right.isFixed()) {
          if (range.min()) {
            propagatePossibleValues(be->left, CexValueData(0, right.min() - 1));
          } else {
            propagatePossibleValues(be->left,
                                    CexValueData(right.min(), maxValue));
          }
        } else {
          // XXX ???
        }
      }
      break;
    }
    case Expr::Ule: {
      BinaryExpr *be = cast<BinaryExpr>(e);

      // XXX heuristic / lossy, what order if conflict

      if (range.isFixed()) {
        ValueRange left = evalRangeForExpr(be->left);
        ValueRange right = evalRangeForExpr(be->right);

        // XXX should deal with overflow (can lead to empty range)

        uint64_t maxValue = bits64::maxValueOfNBits(be->right->getWidth());
        if (left.isFixed()) {
          if (range.min()) {
            propagatePossibleValues(be->right,
                                    CexValueData(left.min(), maxValue));
          } else {
            propagatePossibleValues(be->right, CexValueData(0, left.min() - 1));
          }
        } else if (right.isFixed()) {
          if (range.min()) {
            propagatePossibleValues(be->left, CexValueData(0, right.min()));
          } else {
            propagatePossibleValues(be->left,
                                    CexValueData(right.min() + 1, maxValue));
          }
        } else {
          // XXX ???
        }
      }
      break;
    }

    case Expr::Ne:
    case Expr::Ugt:
    case Expr::Uge:
    case Expr::Sgt:
    case Expr::Sge:
      assert(0 && "invalid expressions (uncanonicalized");

    default:
      break;
    }
  }

  void propagateExactValues(ref<Expr> e, CexValueData range) {
    switch (e->getKind()) {
    case Expr::Constant: {
      // FIXME: Assert that range contains this constant.
      break;
    }

      // Special

    case Expr::NotOptimized:
      break;

    case Expr::Read: {
      ReadExpr *re = cast<ReadExpr>(e);
      const Array *array = re->updates.root;
      CexObjectData &cod = getObjectData(array);
      CexValueData index = evalRangeForExpr(re->index);

      for (const auto *un = re->updates.head.get(); un; un = un->next.get()) {
        CexValueData ui = evalRangeForExpr(un->index);

        // If these indices can't alias, continue propagation
        if (!ui.mayEqual(index))
          continue;

        // Otherwise if we know they alias, propagate into the write value.
        if (ui.mustEqual(index) || re->index == un->index)
          propagateExactValues(un->value, range);
        return;
      }

      // We reached the initial array write, update the exact range if possible.
      if (index.isFixed()) {
        if (ref<ConstantSource> constantSource =
                dyn_cast<ConstantSource>(array->source)) {
          // Verify the range.
          propagateExactValues(constantSource->constantValues[index.min()],
                               range);
        } else if (isa<SymbolicSizeConstantSource>(array->source)) {
          assert(0 && "not implemented");
        } else {
          CexValueData cvd = cod.getExactValues(index.min());
          if (range.min() > cvd.min()) {
            assert(range.min() <= cvd.max());
            cvd = CexValueData(range.min(), cvd.max());
          }
          if (range.max() < cvd.max()) {
            assert(range.max() >= cvd.min());
            cvd = CexValueData(cvd.min(), range.max());
          }
          cod.setExactValues(index.min(), cvd);
        }
      }
      break;
    }

    case Expr::Select: {
      break;
    }

    case Expr::Concat: {
      break;
    }

    case Expr::Extract: {
      break;
    }

      // Casting

    case Expr::ZExt: {
      break;
    }

    case Expr::SExt: {
      break;
    }

      // Binary

    case Expr::And: {
      break;
    }

    case Expr::Or: {
      break;
    }

    case Expr::Xor: {
      break;
    }

      // Comparison

    case Expr::Eq: {
      BinaryExpr *be = cast<BinaryExpr>(e);
      if (range.isFixed()) {
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(be->left)) {
          // FIXME: Handle large widths?
          if (CE->getWidth() <= 64) {
            uint64_t value = CE->getZExtValue();
            if (range.min()) {
              // If the equality is true, then propagate the value.
              propagateExactValue(be->right, value);
            } else {
              // If the equality is false and the comparison is of booleans,
              // then we can infer the value to propagate.
              if (be->right->getWidth() == Expr::Bool)
                propagateExactValue(be->right, !value);
            }
          }
        }
      }
      break;
    }

    // If a boolean not, and the result is known, propagate it
    case Expr::Not: {
      if (e->getWidth() == Expr::Bool && range.isFixed()) {
        propagateExactValue(e->getKid(0), !range.min());
      }
      break;
    }

    case Expr::Ult: {
      break;
    }

    case Expr::Ule: {
      break;
    }

    case Expr::Ne:
    case Expr::Ugt:
    case Expr::Uge:
    case Expr::Sgt:
    case Expr::Sge:
      assert(0 && "invalid expressions (uncanonicalized");

    default:
      break;
    }
  }

  ValueRange evalRangeForExpr(const ref<Expr> &e) {
    CexRangeEvaluator ce(objects);
    return ce.evaluate(e);
  }

  /// evaluate - Try to evaluate the given expression using a consistent fixed
  /// value for the current set of possible ranges.
  ref<Expr> evaluatePossible(ref<Expr> e) {
    return CexPossibleEvaluator(objects).visit(e);
  }

  ref<Expr> evaluateExact(ref<Expr> e) {
    return CexExactEvaluator(objects).visit(e);
  }

  void dump() {
    llvm::errs() << "-- propagated values --\n";
    for (std::map<const Array *, CexObjectData *>::iterator
             it = objects.begin(),
             ie = objects.end();
         it != ie; ++it) {
      const Array *A = it->first;
      ref<ConstantExpr> arrayConstantSize = dyn_cast<ConstantExpr>(A->size);
      if (!arrayConstantSize) {
        klee_warning("Cannot dump %s as it has symbolic size\n",
                     A->getIdentifier().c_str());
      }

      CexObjectData *COD = it->second;

      llvm::errs() << A->getIdentifier() << "\n";
      llvm::errs() << "possible: [";
      for (unsigned i = 0; i < arrayConstantSize->getZExtValue(); ++i) {
        if (i)
          llvm::errs() << ", ";
        llvm::errs() << COD->getPossibleValues(i);
      }
      llvm::errs() << "]\n";
      llvm::errs() << "exact   : [";
      for (unsigned i = 0; i < arrayConstantSize->getZExtValue(); ++i) {
        if (i)
          llvm::errs() << ", ";
        llvm::errs() << COD->getExactValues(i);
      }
      llvm::errs() << "]\n";
    }
  }
};

/* *** */

class FastCexSolver : public IncompleteSolver {
public:
  FastCexSolver();
  ~FastCexSolver();

  PartialValidity computeTruth(const Query &);
  bool computeValue(const Query &, ref<Expr> &result);
  bool computeInitialValues(const Query &,
                            const std::vector<const Array *> &objects,
                            std::vector<SparseStorage<unsigned char>> &values,
                            bool &hasSolution);
};

FastCexSolver::FastCexSolver() {}

FastCexSolver::~FastCexSolver() {}

/// propagateValues - propagate value ranges for the given query and return the
/// propagation results.
///
/// \param query - The query to propagate values for.
///
/// \param cd - The initial object values resulting from the propagation.
///
/// \param checkExpr - Include the query expression in the constraints to
/// propagate.
///
/// \param isValid - If the propagation succeeds (returns true), whether the
/// constraints were proven valid or invalid.
///
/// \return - True if the propogation was able to prove validity or invalidity.
static bool propagateValues(const Query &query, CexData &cd, bool checkExpr,
                            bool &isValid) {
  for (const auto &constraint : query.constraints.cs()) {
    cd.propagatePossibleValue(constraint, 1);
    cd.propagateExactValue(constraint, 1);
  }
  if (checkExpr) {
    cd.propagatePossibleValue(query.expr, 0);
    cd.propagateExactValue(query.expr, 0);
  }

  KLEE_DEBUG(cd.dump());

  // Check the result.
  bool hasSatisfyingAssignment = true;
  if (checkExpr) {
    if (!cd.evaluatePossible(query.expr)->isFalse())
      hasSatisfyingAssignment = false;

    // If the query is known to be true, then we have proved validity.
    if (cd.evaluateExact(query.expr)->isTrue()) {
      isValid = true;
      return true;
    }
  }

  for (const auto &constraint : query.constraints.cs()) {
    if (hasSatisfyingAssignment && !cd.evaluatePossible(constraint)->isTrue())
      hasSatisfyingAssignment = false;

    // If this constraint is known to be false, then we can prove anything, so
    // the query is valid.
    if (cd.evaluateExact(constraint)->isFalse()) {
      isValid = true;
      return true;
    }
  }

  if (hasSatisfyingAssignment) {
    isValid = false;
    return true;
  }

  return false;
}

PartialValidity FastCexSolver::computeTruth(const Query &query) {
  CexData cd;

  bool isValid;
  bool success = propagateValues(query, cd, true, isValid);

  if (!success)
    return PValidity::None;

  return isValid ? PValidity::MustBeTrue : PValidity::MayBeFalse;
}

bool FastCexSolver::computeValue(const Query &query, ref<Expr> &result) {
  CexData cd;

  bool isValid;
  bool success = propagateValues(query, cd, false, isValid);

  // Check if propagation wasn't able to determine anything.
  if (!success)
    return false;

  // FIXME: We don't have a way to communicate valid constraints back.
  if (isValid)
    return false;

  // Propogation found a satisfying assignment, evaluate the expression.
  ref<Expr> value = cd.evaluatePossible(query.expr);

  if (isa<ConstantExpr>(value)) {
    // FIXME: We should be able to make sure this never fails?
    result = value;
    return true;
  } else {
    return false;
  }
}

bool FastCexSolver::computeInitialValues(
    const Query &query, const std::vector<const Array *> &objects,
    std::vector<SparseStorage<unsigned char>> &values, bool &hasSolution) {
  CexData cd;

  bool isValid;
  bool success = propagateValues(query, cd, true, isValid);

  // Check if propagation wasn't able to determine anything.
  if (!success)
    return false;

  hasSolution = !isValid;
  if (!hasSolution)
    return true;

  // propagation found a satisfying assignment, compute the initial values.
  for (unsigned i = 0; i != objects.size(); ++i) {
    const Array *array = objects[i];
    assert(array);
    SparseStorage<unsigned char> data;
    ref<ConstantExpr> arrayConstantSize =
        dyn_cast<ConstantExpr>(cd.evaluatePossible(array->size));
    assert(arrayConstantSize &&
           "Array of symbolic size had not receive value for size!");
    data.resize(arrayConstantSize->getZExtValue());

    for (unsigned i = 0; i < arrayConstantSize->getZExtValue(); i++) {
      ref<Expr> read = ReadExpr::create(
          UpdateList(array, 0), ConstantExpr::create(i, array->getDomain()));
      ref<Expr> value = cd.evaluatePossible(read);

      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(value)) {
        data.store(i, ((unsigned char)CE->getZExtValue(8)));
      } else {
        // FIXME: When does this happen?
        return false;
      }
    }

    values.push_back(data);
  }

  return true;
}

std::unique_ptr<Solver> klee::createFastCexSolver(std::unique_ptr<Solver> s) {
  return std::make_unique<Solver>(std::make_unique<StagedSolverImpl>(
      std::make_unique<FastCexSolver>(), std::move(s)));
}
