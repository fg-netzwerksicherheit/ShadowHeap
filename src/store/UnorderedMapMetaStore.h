#pragma once

#include "metastore.h"
#include <memory>
#include <unordered_map>

template <class Allocator = std::allocator<std::pair<void*, MALLOC_META>>>
class UnorderedMapMetaStore : public IMetaStore {
    using Container =
        std::unordered_map<void*, MALLOC_META, std::hash<void*>, std::equal_to<void*>, Allocator>;
    Container elements{};

public:
    UnorderedMapMetaStore() {
    }

    ~UnorderedMapMetaStore() {
    }

    bool put(MALLOC_META chunk) {
        return chunk.ptr && elements.emplace(chunk.ptr, chunk).second;
    }

    MALLOC_META get(void* key) {
        auto it = elements.find(key);
        if (it == elements.end()) return {};

        return it->second;
    }

    bool remove(MALLOC_META key) {
        auto it = elements.find(key.ptr);
        if (it == elements.end()) return false;
        if (it->second != key) return false;

        elements.erase(it);
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
    using with_allocator = UnorderedMapMetaStore<A<std::pair<void*, MALLOC_META>>>;
};
