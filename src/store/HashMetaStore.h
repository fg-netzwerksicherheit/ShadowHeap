#pragma once

#include "metastore.h"
#include <memory>
#include <unordered_map>

struct PtrHash {
    auto operator()(void* ptr) const noexcept -> size_t {
        return reinterpret_cast<size_t>(ptr) >> 3;
    }
};

template <class Allocator = std::allocator<std::pair<void*, MALLOC_META>>>
class HashMetaStore : public IMetaStore {
    using Container =
        std::unordered_map<void*, size_t, PtrHash, std::equal_to<void*>, Allocator>;
    Container elements{};

public:
    HashMetaStore() {
    }

    ~HashMetaStore() {
    }

    bool put(MALLOC_META chunk) {
        // emplace() inserts the element and returns an <iterator, bool>
        // pair that indicates whether insertion was successful.
        return chunk.ptr && elements.emplace(chunk.ptr, chunk.size).second;
    }

    MALLOC_META get(void* key) {
        auto it = elements.find(key);
        if (it == elements.end()) return {};

        return { key, it->second };
    }

    bool remove(MALLOC_META key) {
        auto it = elements.find(key.ptr);
        if (it == elements.end()) return false;
        if (MALLOC_META{ key.ptr, it->second } != key) return false;

        elements.erase(it);
        return true;
    }

    bool update(MALLOC_META key) {
        auto it = elements.find(key.ptr);
        if (__builtin_expect(it == elements.end(), 0)) return false;

        // update the entry
        it->second = key.size;
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
    using with_allocator = HashMetaStore<A<std::pair<void*, MALLOC_META>>>;
};
