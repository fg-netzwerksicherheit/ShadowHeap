#pragma once

#include "metastore.h"
#include <map>
#include <memory>

template <class Allocator = std::allocator<std::pair<void*, MALLOC_META>>>
class MapMetaStore : public IMetaStore {
    using Container = std::map<void*, MALLOC_META, std::less<void*>, Allocator>;
    Container elements;

public:
    MapMetaStore() : elements{} {
    }

    ~MapMetaStore() {
    }

    bool put(MALLOC_META chunk) {
        return chunk.ptr && elements.emplace(chunk.ptr, chunk).second;
    }

    bool has(void* key) {
        return get(key).ptr != nullptr;
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

    void clear() {
        elements.clear();
    }

    template <template <class V> class A>
    using with_allocator = MapMetaStore<A<std::pair<void*, MALLOC_META>>>;
};
