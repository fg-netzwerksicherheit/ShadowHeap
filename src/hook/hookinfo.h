#pragma once
#include <cassert>
#include <dlfcn.h>

#include "../common/common.h"
#include "../common/malloc_meta.h"

static char* callocbase[1000];
static __thread int recursive;

struct RecursiveRegion {

    RecursiveRegion() {
        ++recursive;
    }

    ~RecursiveRegion() {
        --recursive;
    }
};

struct HookInfo {
    bool isInitialized = false;
    void* (*mallocp)(size_t);
    void* (*callocp)(size_t, size_t);
    void* (*reallocp)(void*, size_t);
    void (*freep)(void*);

    void* call_malloc_raw(size_t len) {
        if (UNLIKELY(!isInitialized)) setupPointers();
        return (*this->mallocp)(len);
    }

    void* call_malloc_recursive(size_t len) {
        RecursiveRegion lock;
        return this->call_malloc_raw(len);
    }

    void* call_malloc_recursive_checked(size_t len) {
        if (UNLIKELY(recursive)) {
            return this->call_malloc_raw(len);
        } else {
            return this->call_malloc_recursive(len);
        }
    }

    void* call_calloc_raw(size_t cnt, size_t len) {
        if (UNLIKELY(!isInitialized)) setupPointers();
        return (*this->callocp)(cnt, len);
    }

    void* call_calloc_recursive(size_t cnt, size_t len) {
        RecursiveRegion lock;
        if (UNLIKELY(!isInitialized)) setupPointers();
        return this->call_calloc_raw(cnt, len);
    }

    void* call_calloc_recursive_checked(size_t cnt, size_t len) {
        if (UNLIKELY(recursive)) {
            if (this->isInitialized == false) {
                return (void*)callocbase;
            } else {
                return NULL;
            }
        } else {
            return this->call_calloc_recursive(cnt, len);
        }
    }

    void* call_realloc_raw(void* ptr, size_t len) {
        if (UNLIKELY(!isInitialized)) setupPointers();
        return (*this->reallocp)(ptr, len);
    }

    void call_free_raw(void* ptr) {
        if (UNLIKELY(!isInitialized)) setupPointers();
        (*this->freep)(ptr);
    }

    template <class T>
    static T dlsym_typed(const char* name) {
        void* fn = dlsym(RTLD_NEXT, name);
        return *reinterpret_cast<T*>(&fn);
    }

    // don't inline this function because it's cold,
    // and will be called at most once
    bool setupPointers() __attribute__((noinline)) {
        if (UNLIKELY(this->isInitialized)) return true;
        assert(this->mallocp = dlsym_typed<decltype(mallocp)>("malloc"));
        assert(this->callocp = dlsym_typed<decltype(callocp)>("calloc"));
        assert(this->reallocp = dlsym_typed<decltype(reallocp)>("realloc"));
        assert(this->freep = dlsym_typed<decltype(freep)>("free"));
        // debug(
        //	"malloc is at: %p\ncalloc is at: %p\nrealloc is at: %p\nfree is "
        //	"at: %p\n",
        //	this->mallocp , this->callocp, this->reallocp, this->freep);
        this->isInitialized = true;
        return true;
    }
};
