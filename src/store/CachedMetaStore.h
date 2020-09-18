#pragma once

#include "MapMetaStore.h"  // as the default fallback store
#include "metastore.h"

#include <array>
#include <cassert>
#include <memory>
#include <vector>

namespace details {
/// A fast but very good hash function for 64-bit values.
/// Code taken from https://nullprogram.com/blog/2018/07/31/
/// by Chris Wellons
/// which is based on http://xoshiro.di.unimi.it/splitmix64.c
/// by Sebatiano Vigna.
inline size_t hash(void* ptr) noexcept {
    auto x = reinterpret_cast<size_t>(ptr);
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    x *= UINT64_C(0x94d049bb133111eb);
    x ^= x >> 31;
    return x;
}

template <class Allocator>
class ResizeableHashMap {
public:
    static constexpr size_t ENTRIES_PER_BIN = 4;
    using Bin = std::array<MALLOC_META, ENTRIES_PER_BIN>;

private:
    static constexpr size_t TYPICAL_CACHE_LINE_SIZE = 64;
    static_assert(sizeof(Bin) <= TYPICAL_CACHE_LINE_SIZE, "each bin should fit into a cache line");

    std::vector<Bin, Allocator> bins;

public:
    /// capacity MUST be a power of 2 and MUST be at least ENTRIES_PER_BIN.
    ResizeableHashMap(size_t capacity) : bins(capacity / ENTRIES_PER_BIN) {
        assert(/* minimum capacity is 1 bin */ capacity >= ENTRIES_PER_BIN);
        assert(/* is power of 2, i.e. single bit is set */ __builtin_popcount(capacity) == 1);
    }
    ~ResizeableHashMap() {
    }

    /// Retrieve the correct bin by hash.
    Bin& get_bin(void* key) noexcept {
        assert(!bins.empty());
        auto mask = bins.size() - 1;  // assuming cache size is power of 2
        auto& bin = bins[hash(key) & mask];
        return bin;
    }

    /// Find an entry using the key, may be NULL if no such entry exists.
    MALLOC_META* get_entry(void* key) noexcept {
        for (auto& entry : get_bin(key))
            if (__builtin_expect(entry.ptr == key, 0)) return &entry;
        return nullptr;
    }

    /// Find an entry for insertion of a new value,
    /// may contain an old value that must first be evicted.
    MALLOC_META& get_insertion_point(void* key) noexcept {
        auto raw_hash = hash(key);
        auto mask = bins.size() - 1;  // assuming cache size is power of 2
        auto& bin = bins[raw_hash & mask];
        for (auto& entry : bin)
            if (__builtin_expect(entry.ptr == nullptr, 0)) return entry;

        // If there is no empty entry, select one entry at random.
        // The leftmost N bits of the hash should be unused,
        // so take them as an RNG.
        static_assert(sizeof(decltype(raw_hash)) == sizeof(uint64_t), "hash must be a 64 bit number");
        constexpr auto entry_mask = ENTRIES_PER_BIN - 1;
        auto entry_i = (raw_hash >> (64 - __builtin_popcount(entry_mask))) & entry_mask;
        return bin[entry_i];
    }

    void clear() {
        for (auto& bin : bins)
            bin = {};
    }

    size_t capacity() const {
        return bins.size() * ENTRIES_PER_BIN;
    }

    void ensure_capacity(size_t required) {
        const auto cap = capacity();
        if (__builtin_expect(required <= cap, 1)) return;

        // Find the factor by which the capacity should be increased.
        // This could theoretically overflow.
        auto factor = 2;
        auto newcap = 2 * cap;
        while (__builtin_expect(required > newcap, 0)) {
            factor *= 2;
            newcap *= 2;
        }

        reserve_double(factor);
    }

private:
    /// factor MUST be a multiple of 2
    ///
    /// Why noinline? Because calling this function has some stack overhead
    /// which isn't needed for ensure_capacity(). Preventing inlining defers this overhead,
    /// but doesn't really add any extra overhead since it will be tail-called.
    void reserve_double(size_t factor) __attribute__((noinline)) {
        const auto oldsize = bins.size();
        const auto newsize = factor * oldsize;

        assert(newsize > oldsize);
        bins.resize(newsize);  // may throw
        const auto newmask = newsize - 1;

        // the existing elements may have to be moved to a new location
        for (size_t bin_i = 0; bin_i < oldsize; ++bin_i) {

            // we already know the likely target bin for the entries
            __builtin_prefetch(&bins[oldsize + bin_i], 1);

            for (size_t entry_i = 0; entry_i < ENTRIES_PER_BIN; ++entry_i) {
                auto& entry = bins[bin_i][entry_i];
                if (__builtin_expect(entry.ptr == nullptr, 0)) continue;

                const auto raw_hash = hash(entry.ptr);
                const auto newbin_i = raw_hash & newmask;

                // when doubling the size, there's a 50% chance of staying in the bin
                if (__builtin_expect(bin_i == newbin_i, 0)) continue;

                // Move the entry to a new bin.
                // It is guaranteed that the new bin `bins[newi]` will contain space:
                // The bin index is determined by bitmasks,
                // so that `i` and `newi` differ by at most one bit (the size was doubled).
                // Consequently, only entries from the `i` bin can be moved to `newi`.

                // Also, it is guaranteed that the first entry of the target bin will be empty.
                if (entry_i == 0) {
                    std::swap(entry, bins[newbin_i][0]);
                    continue;
                }

                for (auto& target : bins[newbin_i]) {
                    if (__builtin_expect(target.ptr == nullptr, 1)) {
                        std::swap(entry, target);
                        break;
                    }
                }
            }
        }
    }
};
}  // namespace details

template <template <class A> class FallbackStore = MapMetaStore, class Allocator = typename std::allocator<MALLOC_META>>
class CachedMetaStore : public IMetaStore {
    size_t cache_entries = 0;
    details::ResizeableHashMap<Allocator> cache;
    FallbackStore<Allocator> fallback_store;

    // assuming that capacity is power of 2
    CachedMetaStore(size_t capacity) : cache(capacity) {
    }

public:
    CachedMetaStore() : CachedMetaStore(128) {
    }

    ~CachedMetaStore() {
    }

    bool put(MALLOC_META chunk) {
        // decide whether to rehash first
        cache.ensure_capacity(size() + 1);

        // insert the chunk, maybe get an old chunk back
        std::swap(chunk, cache.get_insertion_point(chunk.ptr));

        // if the bin was empty, we are done.
        if (__builtin_expect(chunk.ptr == nullptr, 1)) {
            ++cache_entries;
            return true;
        }

        // if the bin contained an old value, insert it into the fallback store
        return fallback_store.put(chunk);
    }

    MALLOC_META get(void* key) {
        MALLOC_META* candidate = cache.get_entry(key);
        if (__builtin_expect(candidate != nullptr, 1)) return *candidate;

        // if the cache didn't contain the value,
        // look into the fallback store
        return fallback_store.get(key);
    }

    bool remove(MALLOC_META key) {
        MALLOC_META* entry = cache.get_entry(key.ptr);

        // if the bin contains a value, just reset it
        if (__builtin_expect(entry != nullptr, 1)) {

            // check that the data is still valid
            if (__builtin_expect(*entry != key, 0)) return false;

            *entry = {};
            --cache_entries;
            return true;
        }

        // otherwise, go to the fallback store
        return fallback_store.remove(key);
    }

    bool update(MALLOC_META key) {
        MALLOC_META* entry = cache.get_entry(key.ptr);

        // if the entry is empty (unlikely), look at the fallback store
        if (__builtin_expect(entry == nullptr, 0))
            return fallback_store.update(key);

        // overwrite the entry with the new value
        *entry = key;

        return true;
    }

    size_t size() {
        return cache_entries + fallback_store.size();
    }

    /// The capacity of the caching layer, not of the entire store.
    size_t capacity() {
        return cache.capacity();
    }

    void reserve(size_t request) {
        cache.ensure_capacity(request);
    }

    void clear() override {
        cache_entries = 0;
        cache.clear();
        fallback_store.clear();
    }

    template <template <class V> class A>
    using with_allocator = CachedMetaStore<FallbackStore, A<MALLOC_META>>;
};
