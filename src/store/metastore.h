#pragma once
#include "../common/malloc_meta.h"

class IMetaStore {
public:
    /// save metadata for a chunk
    virtual bool put(MALLOC_META chunk) = 0;

    /// retrieve metadata for a chunk pointer, may return an empty MALLOC_META
    virtual MALLOC_META get(void* key) = 0;

    /// remove a metadata entry, if it matches
    virtual bool remove(MALLOC_META key) = 0;

    /// update an entry, /// as a more efficient variant of `remove(get(key.ptr)); put(key)`.
    virtual bool update(MALLOC_META key) {
        auto old = get(key.ptr);
        return old.is_some() && remove(old) && put(key);
    }

    /// get the number of currently stored metadata entries
    virtual size_t size() = 0;

    /// try to reserve space up front to prevent internal allocations,
    /// may be implemented as a no-op.
    virtual void reserve(size_t) {
    }

    // Deletes all elements
    virtual void clear() = 0;

    template <template <class V> class Allocator>
    using with_allocator = void;
};
