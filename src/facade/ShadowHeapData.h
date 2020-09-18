#pragma once

#include "../common/common.h"
#include "../common/malloc_meta.h"
#include "../leak/leak.h"
#include "../store/CachedMetaStore.h"
#include "../store/HashMetaStore.h"
#include "../store/InternalAllocator.h"
#include "../store/MapMetaStore.h"
#include "../store/UnorderedMapMetaStore.h"
#include "../store/VectorMetaStore.h"
#include "../store/metastore.h"

#ifndef META_STORE
#define META_STORE CachedMetaStore<>
#endif

#ifndef META_STORE_US
#define META_STORE_US VectorMetaStore<>::with_allocator<InternalAllocator>
#endif

using ConcreteMetaStore = META_STORE::with_allocator<InternalAllocator>;

struct TcacheMetaEntry {
    void* orig_ptr;
    size_t size;
    void* next;
};

class ShadowHeapData {
    bool isInitialized = false;

public:
    //#ifdef PTR_CHECK
    ConcreteMetaStore* store = nullptr;
    //#endif

    //#ifdef TOP_CHECK
    size_t topchunksize = 0;
    //#endif

#ifdef USB_CHECK
    LINKED_LIST_META unsorted[USB_ENTRIES_MAX];
#else
    LINKED_LIST_META* unsorted = nullptr;
#endif
    int unsorted_size = -1;

#ifdef TCA_CHECK
    TcacheMetaEntry tcache[TCACHE_ENTRIES][TCA_BIN_SIZE];
#else
    TcacheMetaEntry** tcache = nullptr;
#endif
    bool tcacheHasData = false;

    ShadowHeapData() {
    }

    ~ShadowHeapData() {
    }

    void ensure_initialized(size_t capacity = 0) {
        if (LIKELY(isInitialized)) return;
#ifdef PTR_CHECK
        store = new ConcreteMetaStore{};
        if (capacity) store->reserve(capacity);
#endif
        this->isInitialized = true;
    }
};
