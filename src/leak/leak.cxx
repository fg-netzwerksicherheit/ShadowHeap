#include "leak.h"
#include "../common/common.h"

namespace {
template <class T>
T* plus_offset(T* original, std::size_t offset) {
    auto address = (char*)original;
    return (T*)(address + offset);
}
}  // namespace

GLIBC_INFO::GLIBC_INFO(HookInfo& hook) {
    valid = 1;

    const char* version = gnu_get_libc_version();
    strncpy(this->version, version, GLIBC_LEN_VERSION - 1);
    strncpy(progname, __progname, PROGNAME_LEN - 1);

    // Since glibc version 2.26 there is one member more in front of fastbinsY.
    // Therefore sizes have to be adjusted accordingly.
    // Also test if tcache is available for appropriate versions

    // for versions 2.24 to 2.25, no adjustments need to be made:
    if (strncmp(version, "2.24", 4) >= 0 && strncmp(version, "2.25", 4) <= 0) {
        offset_adjust_references = 0;
    }

    // for versions 2.26 to 2.30, there is an additional offset
    // and there may be a tcache.
    // But we can't leak the tcache easily!
    else if (strncmp(version, "2.26", 4) >= 0 && strncmp(version, "2.28", 4) < 0) {
        offset_adjust_references = 0x8;
        tcache_present = malloc_leak::test_tcache(hook) ? (void*)1 : 0;
    }
    // Starting with 2.28, tcache entries have a key that lets us leak tcache easily.
    else if (strncmp(version, "2.28", 4) >= 0 && strncmp(version, "2.30", 4) <= 0) {
        offset_adjust_references = 0x8;
        tcache_present = malloc_leak::test_tcache(hook);
    }

    // for other versions, no statement can be made
    else {
        offset_sb0_to_main_arena = 0;
        tcache_present = nullptr;
        valid = 0;
    }

    // sb0-Offset 0x68 is correct up to 2.25 so 0x68 + 0
    // sb0-Offset 0x70 is correct from 2.26 so 0x68 + 8
    offset_sb0_to_main_arena = 0x68 + offset_adjust_references;
}

namespace malloc_leak {

/// Check if a tcache exists.
/// If not: return null
/// If it exists, return the word that corresponds to
/// a tcache entry's `key` field, which is the tcache structure itself.
void* test_tcache(HookInfo& hook) {

    void* barrier1[TEST_SIZE_TCACHEBIN];
    void* buffers1[TEST_SIZE_TCACHEBIN];
    void* buffers2[TEST_SIZE_TCACHEBIN];

    // obtain currently freed tcache objects if present
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        barrier1[i] = hook.call_malloc_raw(TEST_SIZE_TCACHEMALLOC);
    }

    // malloc and free buffers to fill tcache if present
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        buffers1[i] = hook.call_malloc_raw(TEST_SIZE_TCACHEMALLOC);
    }
    memset(buffers1[0], 0, 2 * sizeof(size_t));  // null the memory that might contain an entry key
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        hook.call_free_raw(buffers1[i]);
    }
    // In glibc>2.28, the second word in a tcache entry is a `key`
    // that points back to the tcache itself.
    // In older glibcs, it's just valid but meaningless memory.
    void* might_be_tcache =
        reinterpret_cast<void*>(*(reinterpret_cast<size_t*>(buffers1[0]) + 1));

    // malloc on unsatisfieable size to enforce consolidation
    void* barrier2 = hook.call_malloc_raw(TEST_SIZE_BARRIER);

    // malloc buffers again to compare against buffers1
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        buffers2[i] = hook.call_malloc_raw(TEST_SIZE_TCACHEMALLOC);
    }
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        hook.call_free_raw(buffers2[i]);
    }

    // can free the barriers now
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        hook.call_free_raw(barrier1[i]);
    }
    hook.call_free_raw(barrier2);

    // Test buffers with inverted index. Since tcache is a LIFO (Last in, First out) buffer,
    // we can expect to obtain the same pointers in buffers2, but in reversed order.
    // If that is the case we expect a properly working and initialized tcache inside malloc
    for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
        int inverted_idx = TEST_SIZE_TCACHEBIN - 1 - i;
        if (buffers1[i] == buffers2[inverted_idx]) {
            // debug("[+] Compare %d: %p %p\n", i, buffers1[i], buffers2[inverted_idx]);
        } else {
            // debug("[-] Compare %d: %p %p\n", i, buffers1[i], buffers2[inverted_idx]);
            return nullptr;  // we found a non-matching item
        }
    }

    // we definitely have a tcache now, so make sure to return a true value
    if (might_be_tcache == nullptr) might_be_tcache = (void*)1;
    return might_be_tcache;  // all buffer entries matched, so tcache is present
}

AR_MAIN* leak_arena(GLIBC_INFO& libc_info, HookInfo& hook) {
    if (!libc_info.valid) return nullptr;

    // Initial barrier to force consolidation
    void* barrier0 = hook.call_malloc_raw(TEST_SIZE_BARRIER);

    // Create buffer a embedded between two 'in-use' chunks to avoid consolidation
    void* barrier1 = hook.call_malloc_raw(TEST_SIZE_LEAK);
    char* a = (char*)hook.call_malloc_raw(TEST_SIZE_LEAK);
    void* barrier2 = hook.call_malloc_raw(TEST_SIZE_LEAK);

    // malloc and free buffers to fill tcache if present
    void* fillers[TEST_SIZE_TCACHEBIN];
    if (libc_info.tcache_present) {
        for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
            fillers[i] = malloc(TEST_SIZE_LEAK);
        }
        for (int i = 0; i < TEST_SIZE_TCACHEBIN; i++) {
            hook.call_free_raw(fillers[i]);
        }
    }

    // tcache (if present) is full and therefore a will go to the
    // unsorted_bin After an request which could not be satisfied by
    // chunks in the unsorted_bin a is sorted into smallbin[0] which is
    // a part of 'struct malloc_state' Therefore the exact address of
    // the related arena is now found Calulation: &av + offset =
    // &av->smallbin[0] &av = &av->smallbin[0] - offset
    CHUNKPTR* ptr_a = (CHUNKPTR*)(a - 2 * sizeof(long));
    hook.call_free_raw(a);

    // Consolidate heap by requesting a size which can not be satisfied out of
    // freechunk-bins This should sort our freed chunk a into smallbins[0]
    void* barrier3 = hook.call_malloc_raw(TEST_SIZE_BARRIER);

    // a->fd should now point to the smallbin[0] inside main_arena->bins[]
    // substract static offset libc_info->offset_sb0_to_main_arena to obtain RVA of main_arena
    AR_MAIN* result = (AR_MAIN*)(((char*)ptr_a->fd) - libc_info.offset_sb0_to_main_arena);

    // Clean up
    hook.call_free_raw(barrier1);
    hook.call_free_raw(barrier2);
    hook.call_free_raw(barrier3);
    hook.call_free_raw(barrier0);

    return result;
}

ARENA_INFO* get_arenainfo(HookInfo& hook) {
    GLIBC_INFO libc_info(hook);
    if (!libc_info.valid) return nullptr;

    AR_MAIN* arena = leak_arena(libc_info, hook);
    if (!arena) return nullptr;

    return new ARENA_INFO(hook, arena);
}

}  // namespace malloc_leak

ARENA_INFO::ARENA_INFO(GLIBC_INFO libc_info, AR_MAIN* arena)
    // Adjust pointers by versionized offset (0 bytes up and including 2.25, 8 bytes since 2.26)
    : arena(arena),
      next(plus_offset(&arena->next, libc_info.offset_adjust_references)),
      topchunk(plus_offset(&arena->top, libc_info.offset_adjust_references)),
      last_remainder(plus_offset(&arena->last_remainder, libc_info.offset_adjust_references)),
      unsorted_bin(plus_offset(&arena->bins[0], libc_info.offset_adjust_references)),
      tcache(nullptr),
      valid(false),
      version(libc_info) {

    // if the tcache check returned a value that is trueish and not == 1,
    // then it is a valid pointer
    if ((uintptr_t)libc_info.tcache_present > 1) {
        tcache = (struct tcache_perthread_struct*)libc_info.tcache_present;
    }
#ifdef LEAK_CHECK
    // otherwise, if tcache check return a trueish value,
    // a tcache is present but has to be leaked via the patched libc.
    else if ((uintptr_t)libc_info.tcache_present == 1) {
        unsigned int retTcacheLower = mallopt(-11, 0);
        unsigned int retTcacheUpper = mallopt(-12, 0);
        if (retTcacheLower != 1 && retTcacheUpper != 1) {
            unsigned long tcacheAdr = retTcacheUpper;
            tcacheAdr = tcacheAdr << 32 >> 32;
            tcacheAdr = tcacheAdr << 32;
            tcacheAdr = tcacheAdr + retTcacheLower;
            tcache = (struct tcache_perthread_struct*)tcacheAdr;
        } else {
            // tcache tested and present, but rva still not known!
            // patched libc required but possibly not found!
            // Most likely using system default libc version
            // TODO: Assign appropriate handling!
            // Common usecase: Malloc-Shadow build with LEAK_CHECK but was run with system defaults
            // What should we do then? Print error? Silently ignore tcache? Abort?
        }
    }
#endif

    // check for plausibility
    int checkVal = reinterpret_cast<std::uintptr_t>(this->arena) + libc_info.offset_sb0_to_main_arena;
    if (next && checkVal && this->arena == *next) {
        debug("");
        valid = true;
        return;
    }

    // Try different offset
    this->arena = (AR_MAIN*)(((char*)this->arena) - 0x20);
    if (next && checkVal && this->arena == this->arena->next_free) {
        debug("");
        valid = true;
        return;
    }
}
