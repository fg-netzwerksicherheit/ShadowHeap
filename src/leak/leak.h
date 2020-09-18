#pragma once

#ifndef LEAK_H
#define LEAK_H

#include "../common/common.h"
#include "../hook/hookinfo.h"
#include <dlfcn.h>
#include <gnu/libc-version.h>
#include <malloc.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <type_traits>  // needed for static assert

extern char* __progname;

typedef int __libc_lock_t;

struct CHUNKPTR {
    size_t prevsize;
    size_t size;
    CHUNKPTR* fd;
    CHUNKPTR* bk;
};

struct tcache_entry {
    struct tcache_entry* next;
    CHUNKPTR* ptr;
};

/* There is one of these for each thread, which contains the
   per-thread cache (hence "tcache_perthread_struct").  Keeping
   overall size low is mildly important.  Note that COUNTS and ENTRIES
   are redundant (we could have just counted the linked list each
   time), this is for performance reasons.  */
struct tcache_perthread_struct {
    char counts[TCACHE_ENTRIES];
    tcache_entry* entries[TCACHE_ENTRIES];
};

// in glibc 2.30, the counts changed from char to uint16_t
struct tcache_perthread_struct_2_30 {
    uint16_t counts[TCACHE_ENTRIES];
    tcache_entry* entries[TCACHE_ENTRIES];
};

template <class TcacheLayout>
constexpr inline bool is_valid_tcache_layout() {
    return std::is_same<TcacheLayout, tcache_perthread_struct>::value ||
           std::is_same<TcacheLayout, tcache_perthread_struct_2_30>::value;
}

struct AR_MAIN {
    __libc_lock_t mutex;
    int flags;
    CHUNKPTR* fastbinsY[NFASTBINS];
    CHUNKPTR* top;
    CHUNKPTR* last_remainder;
    CHUNKPTR* bins[NBINS * 2 - 2];
    unsigned int binmap[BINMAPSIZE];
    AR_MAIN* next;
    AR_MAIN* next_free;
    size_t attached_threads;
    size_t system_mem;
    size_t max_system_mem;
};

struct GLIBC_INFO {
    char version[GLIBC_LEN_VERSION];  // Version obtained by glibc-API
    char progname[PROGNAME_LEN];
    void* tcache_present = nullptr;  // trueish if test_tcache() was successful, nullptr otherwise
    int offset_sb0_to_main_arena;  // version specific offset between main_arena and main_arena->sb[0]
    int offset_adjust_references;  // version specific offset between pointers before and after 2.26
    int valid;  // true if version exactly matched and params correctly detected

    GLIBC_INFO(HookInfo& hook);
};

struct ARENA_INFO {
    AR_MAIN* arena;  // the RVA of the main_arena
    AR_MAIN** next;  // Pointer to next arena
    CHUNKPTR** topchunk;  // the RVA of the topchunk
    CHUNKPTR** last_remainder;  // the RVA of the last remainder
    CHUNKPTR** unsorted_bin;  // the RVA of the unsorted_bin
    /// The RVA of the thread local cache -- but take care to cast the layout in 2.30+!
    struct tcache_perthread_struct* tcache;
    int valid = 0;  // true if lglibc_info is valid and arena-test was successful
    GLIBC_INFO version;  // glibc info
    ARENA_INFO(GLIBC_INFO libc_info, AR_MAIN* arena);
};

namespace malloc_leak {
void* test_tcache(HookInfo& hook);
AR_MAIN* leak_arena(GLIBC_INFO* libc_info, HookInfo& hook);
ARENA_INFO* get_arenainfo(HookInfo& hook);
}  // namespace malloc_leak


struct ArenaLeak {
    ARENA_INFO* info = NULL;
    bool isInitialized = false;
    HookInfo hook;
    void* tcacheptr[8];
    void* tcacheptr110[7];
    void* smallbinptr[2];
    void* unsortedbinptr[1];

    ArenaLeak() {
    }

    void ensure_initialized() {
        if (isInitialized) return;

        info = malloc_leak::get_arenainfo(hook);
        if (info && info->valid) {
            this->isInitialized = true;
        } else {
            warn("ShadowHeap: Error: ArenaLeak was not initialized "
                 "correctly!\nAborting...\n");
            std::abort();
        }
        cleanHeap();
    }

    // Removes leftover chunks from leak process
    // Overall memory consumption should be not heavily affected by this
    // ( 6340 bytes total)
    // TODO: Maybe force consolidation.
    void cleanHeap() {
        tcacheptr[7] = malloc(0x1054);
        smallbinptr[0] = malloc(0x50);
        smallbinptr[1] = malloc(0x310);
        for (int i = 0; i < 7; i++) {
            tcacheptr[i] = malloc(0x10);
        }
        for (int i = 0; i < 7; i++) {
            tcacheptr110[i] = malloc(0x100);
        }
        unsortedbinptr[0] = malloc(0x400);
    }

    unsigned long count_unsortedbin(CHUNKPTR** unsorted_bin) {
        unsigned long result = 0;
        CHUNKPTR* single = *unsorted_bin;
        while (!(single->fd == *unsorted_bin)) {
            single = single->bk;
            result++;
        }
        return result;
    }

    void print_unsortedbin(CHUNKPTR** unsorted_bin) {
        CHUNKPTR* single = *unsorted_bin;
        unsigned long n = count_unsortedbin(unsorted_bin);
        info("- unsrtd-bin         [len=%4lu]: ", n);
        if (n > 0) {
            while (!(single->fd == *unsorted_bin)) {
                info("%p", single);
                if (single != *unsorted_bin) {
                    info(",");
                }
                single = single->bk;
            }
        }
        info("\n");
    }

    unsigned long count_tcache_bin(tcache_entry* thebin) {
        unsigned long result = 0;
        tcache_entry* single = thebin;
        while (!(single->next == 0)) {
            single = single->next;
            result++;
        }
        return result + 1;
    }

    template <class TcacheLayout>
    void print_tcache(TcacheLayout* tcache) {
        static_assert(is_valid_tcache_layout<TcacheLayout>(), "TcacheLayout must be one of the known tcache_perthread_struct versions");

        if (tcache != NULL) {
            info("- tcache-bin         [max=%4lu]: \n", TCACHE_ENTRIES);
            for (int i = 0; i < TCACHE_ENTRIES; i = i + 1) {
                tcache_entry* single = tcache->entries[i];
                if (single != 0) {
                    unsigned long binsize = count_tcache_bin(single);
                    info("-- tcache-bin (0x%x) [len=%4lu]: ", ((i + 1) * 0x10) + 0x10, binsize);
                    if (binsize > 0) {
                        while (1) {
                            info("%p", single);
                            if (single->next == 0) {
                                break;
                            } else {
                                info(",");
                                single = single->next;
                            }
                        }
                    }
                    info("\n");
                }
            }
        }
    }

    void print_arenainfo() {
        if (this->info && this->info->version.valid) {
            info("### Leak main_arena ##############\n");
            info("Process    : %s\n", this->info->version.progname);
            info("Version    : %s\n", this->info->version.version);
            info("Offset sb0 : %d\n", this->info->version.offset_sb0_to_main_arena);
            info("Offset ref : %d\n", this->info->version.offset_adjust_references);
            info("tcache     : %d\n", this->info->version.tcache_present);
            info("valid      : %d\n", this->info->valid);
            info("--- arena  : %p\n", this->info->arena);
            info("--- top    : %p => %p\n", this->info->topchunk, *this->info->topchunk);
            info(
                "--- last   : %p => %p\n", this->info->last_remainder,
                *this->info->last_remainder);
            info(
                "--- usb    : %p => %p\n", this->info->unsorted_bin, *this->info->unsorted_bin);
            info("--- next   : %p => %p\n", this->info->next, *this->info->next);
            info("--- tcache : %p \n", this->info->tcache);
            info("Bins       :\n");
            print_unsortedbin(this->info->unsorted_bin);
            if (strncmp(info->version.version, "2.30", 4) < 0)  // account for tcache layout
                print_tcache(this->info->tcache);
            else
                print_tcache((tcache_perthread_struct_2_30*)this->info->tcache);
            info("##################################\n");
        } else {
            warn("ERROR: Invalid version struct: %p\n", this->info);
        }
    }
};

#endif
