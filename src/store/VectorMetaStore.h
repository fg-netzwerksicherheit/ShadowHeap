#pragma once

#include "metastore.h"
#include <algorithm>
#include <memory>
#include <vector>

#ifdef VERBOSE
#define debug(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

template <class Allocator = std::allocator<MALLOC_META>>
class VectorMetaStore : public IMetaStore {
    using Container = std::vector<MALLOC_META, Allocator>;
    Container elements;

public:
    VectorMetaStore() {
    }

    ~VectorMetaStore() {
    }

    bool put(MALLOC_META chunk) {
        if (chunk.ptr && !get(chunk.ptr).ptr) {
            elements.emplace_back(chunk);
            return true;
        }

        return false;
    }

    MALLOC_META get(void* key) {
        for (auto& chunk : elements) {
            if (chunk.ptr == key) return chunk;
        }

        return {};
    }

    bool remove(MALLOC_META key) {
        auto it = std::find_if(elements.begin(), elements.end(), [&](auto el) {
            // debug("META KEY   (RM): %16p %16p %zu\n", &key, key.ptr, key.size);
            // debug("META STORE (RM): %16p %16p %zu\n", &el, el.ptr, el.size);
            return el.ptr == key.ptr;
        });
        if (it == elements.end()) return false;
        if (*it != key) return false;

        // remove by swapping with last element and erasing the end
        using std::swap;
        swap(*it, elements.back());
        elements.pop_back();

        return true;
    }

    size_t size() {
        return elements.size();
    }

    void reserve(size_t size) override {
        elements.reserve(size);
    }

    void clear() {
        elements.clear();
    }

    template <template <class V> class A>
    using with_allocator = VectorMetaStore<A<MALLOC_META>>;
};
