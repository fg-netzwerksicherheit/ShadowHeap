#pragma once

#include "../common/common.h"
#include "../common/malloc_meta.h"
#include "../facade/ShadowHeapFacade.h"
#include "../hook/hookinfo.h"

class ShadowHeapWrapper {
public:
    ShadowHeapFacade facade;
    struct HookInfo info;

    ShadowHeapWrapper() {
        facade.ensure_initialized();
    }

    ~ShadowHeapWrapper() {
    }

    void free(void* ptr) __attribute__((flatten)) {
        // free(NULL) is legal
        if (UNLIKELY(ptr == nullptr)) return;
#ifdef SHADOW
        facade.free_pre(ptr);
        info.call_free_raw(ptr);
        facade.free_post(ptr);
#else
        info.call_free_raw(ptr);
#endif
    }

    void* malloc(size_t len) __attribute__((flatten)) {
        void* ret = NULL;
#ifdef SHADOW
        facade.malloc_pre(len);
        ret = info.call_malloc_recursive_checked(len);
        facade.malloc_post(len, ret);
#else
        ret = info.call_malloc_recursive_checked(len);
#endif
        return ret;
    }

    void* calloc(size_t cnt, size_t len) __attribute__((flatten)) {
        void* ret = NULL;
#ifdef SHADOW
        facade.calloc_pre(cnt, len);
        ret = info.call_calloc_recursive_checked(cnt, len);
        facade.calloc_post(cnt, len, ret);
#else
        ret = info.call_calloc_recursive_checked(cnt, len);
#endif
        return ret;
    }

    void* realloc(void* ptr, size_t len) __attribute__((flatten)) {

        // realloc() is a complicated function with different modes.
        // * can resize memory (normal use case)
        // * if len == 0, free()s memory
        // * if ptr == NULL, malloc()s memory
        // The simple cases can be delegated right here:
        if (UNLIKELY(ptr == nullptr)) return malloc(len);
        if (UNLIKELY(len == 0)) {
            free(ptr);
            return nullptr;
        }

        void* ret = NULL;
#ifdef SHADOW
        facade.realloc_pre(ptr, len);
        ret = malloc_memcpy_free_approach(ptr, len);
        facade.realloc_post(ptr, len, ret);
#else
        ret = info.call_realloc_raw(ptr, len);
#endif
        return ret;
    }

    // cannot simply do:
    // void* ret = (*reallocp)(ptr, len);
    // because we must first verify that the pointer is up to date.
    // This isn't exactly optimal,
    // but as a start let's use a malloc-memcpy-free approach instead.

    void* malloc_memcpy_free_approach(void* ptr, size_t len) {
        // if (ptr) {
        //     auto header = CHUNK_HEADER::from_memory(ptr);
        //     auto meta = store->get(ptr);
        //     if (!meta)
        //         abort();
        //     if (*meta != MALLOC_META::from_chunk_header(*header))
        //         abort();
        //     void* ret = reallocp(ptr, len);
        //     auto new_header = CHUNK_HEADER::from_memory(ret);
        //     if (ret == ptr) {
        //         *meta = MALLOC_META::from_chunk_header(new_header);
        //     } else {
        //         store->remove(ptr);
        //         store->put(MALLOC_META::from_chunk_header(new_header));
        //     }
        //     return ret;
        // } else {
        //     return malloc(len);
        // }

        void* ret = info.call_malloc_raw(len);
        facade.realloc_mallochandler(ret, len);

        // figure out how much to copy
        auto header = CHUNK_HEADER::from_memory(ptr);
        size_t copy_this_much = header->useable_size();
        if (len < copy_this_much) copy_this_much = len;

        // // sanity checks:
        // assert(copy_this_much <= CHUNK_HEADER::from_memory(ptr)->useable_size());
        // assert(copy_this_much <= CHUNK_HEADER::from_memory(ret)->useable_size());
        // assert(copy_this_much <= len);
        // assert(copy_this_much > 0);  // useable_size and len are both non-zero
        // *((volatile char*) ptr + (copy_this_much - 1));  // ensure dereferenceability of last ptr byte
        // *((volatile char*) ret + (copy_this_much - 1));  // ensure dereferenceability of last ret byte

        memcpy(ret, ptr, copy_this_much);
        facade.realloc_freehandler(ptr);

        return ret;
    }
};
