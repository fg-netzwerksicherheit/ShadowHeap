
#include "../hook/hookinfo.h"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>


namespace {

/// The InternalAllocator class is a C++ allocator
/// that uses the *original* malloc implementation,
/// without going through or wrappers.
/// This avoids unexpected pointers turning up in a free().

template <class T>
struct InternalAllocator {
    using value_type = T;

    InternalAllocator() = default;
    struct HookInfo info;

    template <class U>
    constexpr InternalAllocator(InternalAllocator<U> const&) {
    }

    T* allocate(std::size_t n) noexcept {

        if (info.isInitialized == false) {
            info.setupPointers();
            // debug("Allocator initialized!\n");
        }

        if (n <= std::numeric_limits<std::size_t>::max() / sizeof(T)) {
            if (auto p = info.call_malloc_raw(n * sizeof(T))) {
                // debug("internal allocate() = %p\n", p);
                return static_cast<T*>(p);
            } else {
                std::fprintf(stderr, "ShadowHeap: ERROR: internal malloc() failed\n");
            }
        } else {
            std::fprintf(stderr, "ShadowHeap: ERROR: internal allocate() impossibly large");
        }

        // if an internal allocation fails,
        // there's nothing left but to crash.
        std::abort();
    }

    void deallocate(T* p, std::size_t) noexcept {
        // debug("internal deallocate(%p)\n", p);
        info.call_free_raw(p);
    }
};

}  // namespace
