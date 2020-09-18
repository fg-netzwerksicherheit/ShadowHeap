
#include "../store/CachedMetaStore.h"
#include "../store/MapMetaStore.h"
#include "../store/UnorderedMapMetaStore.h"
#include "../store/VectorMetaStore.h"
#include "../tests/tap.h"

#include <ostream>
#include <utility>

using namespace std;

static auto operator<<(std::ostream& out, std::pair<bool, MALLOC_META> value)
    -> std::ostream& {
    return out << '<' << value.first << ", " << value.second << '>';
}

constexpr size_t SUBTESTS = 15;

void test_Store(TAP& tap, IMetaStore& store) {
    void* key1 = (void*)1234;
    void* key2 = (void*)4321;
    MALLOC_META chunk1{ key1, 17 };
    MALLOC_META chunk2{ key2, 130 };

    tap.ok_eq(store.size(), 0u, "size() == 0");
    tap.ok(store.put(chunk1), "put(chunk1)");
    tap.ok(store.put(chunk2), "put(chunk2)");
    tap.ok_eq(store.get(key1), chunk1, "get(key1)");
    tap.ok_eq(store.get(key2), chunk2, "get(key2)");
    tap.ok_eq(store.get((void*)171819), MALLOC_META{}, "get(garbage) fails");
    tap.ok_eq(store.size(), 2u, "size() == 2");

    tap.ok(!store.update({ (void*)(666), 1234 }), "update(garbage) fails");
    tap.ok(store.update({ key2, 141 }), "update(chunk2)");

    tap.ok(!store.remove({ key1, 1234 }), "remove(manipulated chunk1) fails");
    tap.ok(!store.remove({ key2, 130 }), "remove(old chunk2) fails");
    tap.ok(!store.remove({ (void*)443399, 17 }), "remove(nonexistent) fails");
    tap.ok(store.remove({ key1, 17 }), "remove(chunk1) works");
    tap.ok(store.remove({ key2, 141 }), "remove(updated chunk2) works");
    tap.ok_eq(store.size(), 0u, "size() == 0");
}

void test_Cached_reserve(TAP& tap) {
    tap.subtest("CachedMetaStore can reserve storage up front", 7, [](TAP& tap) {
        CachedMetaStore<MapMetaStore> store;

        MALLOC_META chunk1{ (void*)1234, 83 };
        MALLOC_META chunk2{ (void*)(43234 << 3), 9382 };

        store.put(chunk1);
        store.put(chunk2);

        tap.ok_eq(store.size(), 2u, "size() == 2");
        tap.ok_eq(store.capacity(), 128u, "capacity() == 128");

        store.reserve(129);  // should cause rehash to 256

        tap.ok_eq(store.size(), 2u, "size() == 2 after rehash");

        tap.ok(store.remove(chunk1), "remove(chunk1)");
        tap.ok(store.remove(chunk2), "remove(chunk2)");
        tap.ok_eq(store.size(), 0u, "size() == 0");
        tap.ok_eq(store.capacity(), 256u, "capacity() == 256");
    });
}

void test_Cached_rehash(TAP& tap) {
    tap.subtest("CachedMetaStore will rehash when space is limited", 7, [](TAP& tap) {
        CachedMetaStore<> store;
        tap.ok_eq(store.capacity(), 128u, "initial capacity is 128");

        auto make_example_chunk = [](size_t i) -> MALLOC_META {
            // note that the ptr must be non-null
            return { (void*)(32 + 8 * i), 13 + i };
        };

        for (size_t i = 0; i < 129; i++) {
            auto chunk = make_example_chunk(i);
            if (!store.put(chunk)) {
                tap.note() << "insertion " << i << " failed: " << chunk << std::endl;
                break;
            }
            if (store.size() != i + 1) {
                tap.note() << "insertion " << i << " out of sync: " << chunk << std::endl;
                break;
            }
        }

        tap.ok_eq(store.size(), 129u, "added 129 elements");
        tap.ok_eq(store.capacity(), 256u, "adding so much elements caused a rehash");

        MALLOC_META chunk;
        for (size_t i = 0; i < 129; i++) {
            auto expected = make_example_chunk(i);
            chunk = store.get(expected.ptr);
            if (chunk != expected) break;
            chunk = {};
        }
        tap.ok(!chunk.ptr, "retrieving stored chunks") ||
            tap.note() << "for item: " << chunk << std::endl;

        bool remove_ok = true;
        for (size_t i = 0; i < 129u; i++)
            remove_ok &= store.remove(make_example_chunk(i));
        tap.ok(remove_ok, "removing stored chunks");

        tap.ok_eq(store.size(), 0u, "no elements remain");
        tap.ok_eq(store.capacity(), 256u, "cache capacity is unchanged");
    });
}

int main(int argc, char** argv) {
    TAP tap{ 6 };

    tap.subtest("VectorMetaStore", SUBTESTS, [](TAP& tap) {
        VectorMetaStore<> store;
        test_Store(tap, store);
    });

    tap.subtest("MapMetaStore", SUBTESTS, [](TAP& tap) {
        MapMetaStore<> store;
        test_Store(tap, store);
    });

    tap.subtest("UnorderedMapMetaStore", SUBTESTS, [](TAP& tap) {
        UnorderedMapMetaStore<> store;
        test_Store(tap, store);
    });

    tap.subtest("CachedMetaStore", SUBTESTS, [](TAP& tap) {
        CachedMetaStore<MapMetaStore> store;
        test_Store(tap, store);
    });

    test_Cached_reserve(tap);
    test_Cached_rehash(tap);

    return !tap.print_result();
}
