#ifndef KLEE_ORDERMAINTENANCETRIE_H
#define KLEE_ORDERMAINTENANCETRIE_H
#include <bitset>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <memory>
#include <vector>

#define ITEMSIZE 64 // depth of tree
#define ALPHA 1.4   // balancing constant

namespace klee {

class OrderMaintenanaceTree {
private:
  struct Node {

    size_t height = 0;
    size_t weight = 0;
    size_t tag = 0;

    Node *parent = nullptr;
    Node *left = nullptr;
    Node *right = nullptr;

    Node(size_t h = 0, Node *p = nullptr) {
      parent = p;
      height = h;
    }

    bool isFull() {
      return std::pow(0.5, ITEMSIZE - height) * weight >
             std::pow(1.0 / ALPHA, height);
    }
    ~Node() {
      if (left)
        delete left;
      if (right)
        delete right;
      if (parent) {
        if (this == parent->left)
          parent->left = nullptr;
        if (this == parent->right)
          parent->right = nullptr;
      }
      Node *current = parent;
      while (current) {
        current->weight -= weight;
        current = current->parent;
      }
    }
  };

private:
  Node *root;

private:
  Node *insert(size_t x) const {
    std::bitset<ITEMSIZE> xbin(x);
    Node *current = root;
    for (int i = ITEMSIZE - 1; i > -1; i--) {
      current->weight++;
      if (!xbin[i]) {
        if (!current->left)
          current->left = new Node(current->height + 1, current);
        current = current->left;
      } else {
        if (!current->right)
          current->right = new Node(current->height + 1, current);
        current = current->right;
      }
    }
    current->tag = x;
    return current;
  }

  Node *find(size_t x) const {
    std::bitset<ITEMSIZE> xbin(x);
    Node *current = root;
    for (int i = ITEMSIZE - 1; i > -1; i--) {
      if (!xbin[i]) {
        if (!current->left)
          return nullptr;
        current = current->left;
      } else {
        if (!current->right)
          return nullptr;
        current = current->right;
      }
    }
    return current;
  }

  Node *getNext(Node *x) const {
    Node *current = x;
    assert(current && "No such item in trie");
    while (current->parent &&
           (current == current->parent->right || !current->parent->left ||
            !current->parent->right)) {
      current = current->parent;
    }
    if (!current->parent)
      return nullptr;

    current = current->parent->right;
    while (current->height != ITEMSIZE) {
      if (current->left)
        current = current->left;
      else if (current->right)
        current = current->right;
      else
        assert(false);
    }
    return current;
  }

  Node *getNext(size_t x) const {
    Node *current = find(x);
    return getNext(current);
  }

  bool getNextItem(size_t x, size_t &result) const {
    if (!getNext(x))
      return false;
    Node *next = getNext(x);
    result = next->tag;
    return true;
  }

  void findAllLeafs(Node *item, std::vector<Node *> &result) {
    if (item->height == ITEMSIZE)
      result.push_back(item);
    if (item->left) {
      findAllLeafs(item->left, result);
    }
    if (item->right) {
      findAllLeafs(item->right, result);
    }
  }

  void rebalance(Node *item) {
    while (item->parent && item->parent->isFull()) {
      item = item->parent;
    }
    assert(item->parent && "Wrong choice of constants");
    std::vector<Node *> leafs;
    findAllLeafs(item, leafs);
    assert(leafs.size() > 0);
    size_t min;
    min = leafs[0]->tag;
    min = min >> (ITEMSIZE - item->height) << (ITEMSIZE - item->height);
    size_t diff = ((1 << (ITEMSIZE - item->height)) - 1) / leafs.size();

    for (Node *i : leafs) {
      i->parent->left = nullptr;
      i->parent->right = nullptr;
    }
    delete item;
    for (Node *i : leafs) {
      Node *newItem = insert(min);
      *i = *(newItem);
      Node *parent = newItem->parent;
      if (newItem == parent->left) {
        delete newItem;
        parent->left = i;
      } else {
        delete newItem;
        parent->right = i;
      }
      min += diff;
    }
  }

public:
  struct Iterator {
    using iterator_category = std::input_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = size_t;
    using pointer = size_t *;
    using reference = size_t &;

  private:
    OrderMaintenanaceTree::Node *leaf;
    const OrderMaintenanaceTree *owner;

  public:
    Iterator(OrderMaintenanaceTree::Node *leaf = nullptr,
             const OrderMaintenanaceTree *owner = nullptr)
        : leaf(leaf), owner(owner) {}

    Iterator &operator++() {
      if (!owner->getNext(leaf)) {
        leaf = nullptr;
        owner = nullptr;
        return *this;
      }
      leaf = owner->getNext(leaf);
      return *this;
    }

    bool operator==(const Iterator &other) const { return leaf == other.leaf; }

    bool operator!=(const Iterator &other) const { return !(*this == other); }

    size_t operator*() const {
      assert(leaf);
      return leaf->tag;
    }
  };

public:
  Iterator begin() { return Iterator(find(0), this); }
  Iterator end() { return Iterator(nullptr, nullptr); }

  OrderMaintenanaceTree() {
    root = new Node();
    insert(0);
  }

  ~OrderMaintenanaceTree() {
    std::vector<Node *> leafs;
    findAllLeafs(root, leafs);
    delete root;
  }

  Iterator insertAfter(Iterator it) {
    size_t next = 0;
    size_t p = *it;
    if (!getNextItem(p, next) || next != p + 1) {
      return Iterator(insert(p + 1), this);
    }
    Node *found = find(p);
    rebalance(found);
    return Iterator(insert(found->tag + 1), this);
  }
};
} // namespace klee

#undef ITEMSIZE
#undef ALPHA
#endif