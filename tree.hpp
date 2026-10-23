#pragma once
#include <vector>

template<typename T>
struct Node {
    T data;
    std::vector<Node<T>*> next;
    Node(const T& d) : data(d) {}
    ~Node() {
        for (auto& child : next) {
            delete child;
        }
    }
    size_t getDepth() const {
        if (next.empty()) return 1;
        size_t max_depth = 0;
        for (const auto& child : next) {
            if (child) {
                max_depth = std::max(max_depth, child->getDepth());
            }
        }
        return max_depth + 1;
    }
    size_t countNodes() const {
        size_t count = 1; // Count the current node
        for (const auto& child : next) {
            if (child) {
                count += child->countNodes();
            }
        }
        return count;
    }
};