#include "../common/malloc_meta.h"
#include "tap.h"
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <unistd.h>

enum class IsMmapped { No, Yes };

struct ReallocState {
    size_t size;
    IsMmapped is_mmapped = IsMmapped::No;
};

void test_realloc(TAP& tap, const char* name, ReallocState inistate, ReallocState newstate) {
    size_t extra_tests = 0 + 2 * (inistate.is_mmapped == IsMmapped::No) +
                         2 * (newstate.is_mmapped == IsMmapped::No);
    auto inisize = inistate.size;
    auto newsize = newstate.size;

    tap.subtest(name, 6 + extra_tests, [&](TAP& tap) {
        const char fill = 0x23;
        void* oldptr = std::malloc(inisize);
        std::memset(oldptr, fill, inisize);

        // verify metadata around the old ptr
        {
            auto old_header = CHUNK_HEADER::from_memory(oldptr);

            switch (inistate.is_mmapped) {
            case IsMmapped::No:
                tap.ok(!old_header->is_mmapped(), "old chunk is not mmapped");
                tap.ok(old_header->is_main_arena(), "old chunk is main arena");
                tap.ok(old_header->next_chunk()->is_prev_inuse(), "old next chunk is prev inuse");
                break;
            case IsMmapped::Yes:
                tap.ok(old_header->is_mmapped(), "old chunk is mmapped");
            }

            tap.ok(old_header->useable_size() >= inisize, "old chunk size must include memory + header") ||
                tap.note() << "chunksize=" << old_header->chunksize()
                           << " inisize=" << inisize << " diff="
                           << static_cast<ssize_t>(old_header->useable_size() - inisize)
                           << std::endl;
        }

        // realloc the memory
        void* newptr = std::realloc(oldptr, newsize);

        tap.ok(oldptr != newptr, "due to shadowheap, realloc() always produces different pointer") ||
            tap.note() << "old=" << oldptr << " new=" << newptr << std::endl;

        {
            auto new_header = CHUNK_HEADER::from_memory(newptr);

            switch (newstate.is_mmapped) {
            case IsMmapped::No:
                tap.ok(!new_header->is_mmapped(), "new chunk is not mmapped");
                tap.ok(new_header->is_main_arena(), "new chunk is main arena");
                tap.ok(new_header->next_chunk()->is_prev_inuse(), "new next chunk is prev inuse");
                break;
            case IsMmapped::Yes:
                tap.ok(new_header->is_mmapped(), "new chunk is mmapped");
            }

            tap.ok(new_header->useable_size() >= newsize, "new chunk size must include memory + header") ||
                tap.note() << "chunksize=" << new_header->chunksize()
                           << " newsize=" << newsize << " diff="
                           << static_cast<ssize_t>(new_header->useable_size() - newsize)
                           << std::endl;
        }

        // verify that memory was properly copied
        bool all_ok = true;
        auto common_size = std::min(inisize, newsize);
        for (char* p = (char*)newptr; p != (char*)newptr + common_size; ++p) {
            auto c = *p;
            if (c != fill) {
                all_ok = false;
                tap.fail("memory was properly copied");
                tap.note() << "ptr=" << (void*)p << " c=" << c
                           << " offset=" << ((char*)newptr - p) << std::endl;
                break;
            }
        }
        if (all_ok) {
            tap.pass("memory was properly copied");
        }

        std::free(newptr);
    });
}

int main() {
    TAP tap{ 5 };

    // set fixed mmap threshold to 16kB
    if (!mallopt(M_MMAP_THRESHOLD, 16 * 1024)) {
        tap.note() << "setting mallopt() failed" << std::endl;
        return 1;
    }

    tap.subtest("realloc() small size increases", 10, [](TAP& tap) {
        test_realloc(tap, "can realloc 0x20 -> 0x30", { 0x20 }, { 0x30 });
        test_realloc(tap, "can realloc 0x20 -> 0x29", { 0x20 }, { 0x29 });
        test_realloc(tap, "can realloc 0x20 -> 0x28", { 0x20 }, { 0x28 });
        test_realloc(tap, "can realloc 0x20 -> 0x27", { 0x20 }, { 0x27 });
        test_realloc(tap, "can realloc 0x20 -> 0x26", { 0x20 }, { 0x26 });
        test_realloc(tap, "can realloc 0x20 -> 0x25", { 0x20 }, { 0x25 });
        test_realloc(tap, "can realloc 0x20 -> 0x24", { 0x20 }, { 0x24 });
        test_realloc(tap, "can realloc 0x20 -> 0x23", { 0x20 }, { 0x23 });
        test_realloc(tap, "can realloc 0x20 -> 0x22", { 0x20 }, { 0x22 });
        test_realloc(tap, "can realloc 0x20 -> 0x21", { 0x20 }, { 0x21 });
    });

    test_realloc(tap, "can realloc 0x20 -> 0x20", { 0x20 }, { 0x20 });

    tap.subtest("realloc() small size decreases", 9, [](TAP& tap) {
        test_realloc(tap, "can realloc 0x30 -> 0x29", { 0x30 }, { 0x29 });
        test_realloc(tap, "can realloc 0x30 -> 0x28", { 0x30 }, { 0x28 });
        test_realloc(tap, "can realloc 0x30 -> 0x27", { 0x30 }, { 0x27 });
        test_realloc(tap, "can realloc 0x30 -> 0x26", { 0x30 }, { 0x26 });
        test_realloc(tap, "can realloc 0x30 -> 0x25", { 0x30 }, { 0x25 });
        test_realloc(tap, "can realloc 0x30 -> 0x24", { 0x30 }, { 0x24 });
        test_realloc(tap, "can realloc 0x30 -> 0x23", { 0x30 }, { 0x23 });
        test_realloc(tap, "can realloc 0x30 -> 0x22", { 0x30 }, { 0x22 });
        test_realloc(tap, "can realloc 0x30 -> 0x21", { 0x30 }, { 0x21 });
    });

    tap.subtest("realloc() huge size increases", 11, [](TAP& tap) {
        // ensure that there is a free chunk instead of the top chunk
        auto guard1 = std::malloc(17 * 1024);
        auto guard2 = std::malloc(17 * 1024);
        std::free(guard1);

        test_realloc(
            tap, "can realloc 40kB -> 80kB", { 40 * 1024, IsMmapped::Yes },
            { 80 * 1024, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB -> 40kB + 1", { 40 * 1024, IsMmapped::Yes },
            { 40 * 1024 + 1, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB -> 40kB + 2", { 40 * 1024, IsMmapped::Yes },
            { 40 * 1024 + 2, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB -> 40kB + 7", { 40 * 1024, IsMmapped::Yes },
            { 40 * 1024 + 7, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB -> 40kB + 8", { 40 * 1024, IsMmapped::Yes },
            { 40 * 1024 + 8, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB -> 40kB + 9", { 40 * 1024, IsMmapped::Yes },
            { 40 * 1024 + 9, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB -> 40kB + 1 page", { 40 * 1024, IsMmapped::Yes },
            { 40 * 1024 + static_cast<size_t>(sysconf(_SC_PAGESIZE)), IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB - 1 -> 40kB",
            { 40 * 1024 - 1, IsMmapped::Yes }, { 40 * 1024, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB - 2 -> 40kB",
            { 40 * 1024 - 2, IsMmapped::Yes }, { 40 * 1024, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB - 8 -> 40kB",
            { 40 * 1024 - 8, IsMmapped::Yes }, { 40 * 1024, IsMmapped::Yes });
        test_realloc(
            tap, "can realloc 40kB - 1 page -> 40kB",
            { 40 * 1024 - static_cast<size_t>(sysconf(_SC_PAGESIZE)), IsMmapped::Yes },
            { 40 * 1024, IsMmapped::Yes });

        std::free(guard2);
    });

    tap.subtest("prevsize field overlaps with data of previous chunk", 3, [](TAP& tap) {
        struct Filler {
            size_t a;
            size_t b;
            size_t c;
        };

        volatile Filler* p = (Filler*)std::malloc(sizeof(Filler));
        volatile Filler* q = (Filler*)std::malloc(sizeof(Filler));
        auto* header = CHUNK_HEADER::from_memory((void*)p);
        tap.ok_eq(header->chunksize(), 32ul, "minimum chunk size");

        p->c = 123;
        tap.ok_eq(header->next_chunk()->prev_size, 123ul, "prevsize is 123");

        p->c = 789;
        tap.ok_eq(header->next_chunk()->prev_size, 789ul, "prevsize is 789");

        std::free((void*)p);
        std::free((void*)q);
    });


    return !tap.result_ok();
}
