#pragma once

#include <cstdlib>
#include <signal.h>

#include "../common/common.h"
#include "../common/malloc_meta.h"
#include "../common/version.h"
#include "../facade/ShadowHeapData.h"
#include "../leak/leak.h"
#include "../tools/ModeReader.h"

// Prevent call during facade_initialization
#define CHECK_EARLY_RETURN                                                     \
    if (NOT_YET_INITIALIZED) return;

// Prevent calls during facade initialization
#define NOT_YET_INITIALIZED UNLIKELY(!this->isInitialized)

// Static reference to facade for callbackHandler
// void handlerCallback(int from, int to, void* ptr);


class ShadowHeapFacade {
    bool isInitialized = false;
    bool running_under_2_30_or_later = false;

public:
    struct HookInfo info;
    struct ArenaLeak leak;
    ModeReader modes;
    ShadowHeapData data;

    ShadowHeapFacade() {
    }

    ~ShadowHeapFacade() {
    }

    void ensure_initialized() {
        // only perform initialization once
        if (LIKELY(isInitialized)) return;

        modes.ensure_initialized();
        data.ensure_initialized(this->modes.initialStoreSize);
        leak.ensure_initialized();

        running_under_2_30_or_later =
            (strncmp(leak.info->version.version, "2.30", 4) >= 0);

        // Read lib mode
        if (modes.leakMode) {
            // long basePtr = (long)&handlerCallback;
            // int lower = basePtr << 32 >> 32;
            // int upper = (int)(basePtr >> 32);
            // int retLower = mallopt(-9, (int)lower);
            // int retUpper = mallopt(-10, (int)upper);
            // if (!retLower || !retUpper ) {
            //    this->modes.leakMode = false;
            //}
        }
        if (modes.tcaMode) {
            if (!this->modes.leakMode) {
                this->modes.tcaMode = false;
            }
        }
        // this->modes.ptrMode = false;
        // getchar();

        // Print results
        info("----------------------------------\n");
        info("Version      : %s\n", LIB_VERSION);
        info("PTR Mode     : %d\n", this->modes.ptrMode);
        info("TOP Mode     : %d\n", this->modes.topMode);
        info("USB Mode     : %d\n", this->modes.usbMode);
        info("TCA Mode     : %d\n", this->modes.tcaMode);
        info("LEAK Mode    : %d\n", this->modes.leakMode);
        leak.print_arenainfo();
        info("----------------------------------\n");
        isInitialized = true;
    }

    void store_tcache() {
#ifdef TCA_CHECK
        if (LIKELY(!running_under_2_30_or_later))
            _store_tcache_impl(leak.info->tcache);
        else
            _store_tcache_impl((tcache_perthread_struct_2_30*)leak.info->tcache);
#endif
    }

    template <class TcacheLayout>
    void _store_tcache_impl(TcacheLayout* tcache) {
        static_assert(is_valid_tcache_layout<TcacheLayout>(), "TcacheLayout must be valid tcache_perthread_struct");
        if (UNLIKELY(!isInitialized)) return;
        if (LIKELY(!this->modes.tcaMode)) return;

        // Use leaked bin
        if (UNLIKELY(tcache == nullptr)) return;

        // Iterate entries in tcache struct
        for (int i = 0; i < TCACHE_ENTRIES; i++) {
            struct tcache_entry* entry = tcache->entries[i];
            if (UNLIKELY(entry == nullptr)) continue;
            if (UNLIKELY(tcache->counts[i] <= 0)) continue;

            // Iterate elements in bin
            info("TCA     (STR ) %p (%d) => %d element(s): ", tcache, i, tcache->counts[i]);
            for (int b = 0; b < TCA_BIN_SIZE; b++) {

                // Store metadata from hdr into bucket
                // Store related ptr in bucket->bk field which is unused at that time
                CHUNK_HEADER* hdr = CHUNK_HEADER::from_memory(entry);
                TcacheMetaEntry* bucket = &data.tcache[i][b];

                // Copy the data but skip prev_size field because it might be not valid
                bucket->orig_ptr = entry;
                bucket->size = hdr->chunksize();
                bucket->next = hdr->fd;

                data.tcacheHasData = true;
                info("%p", entry);

                if (LIKELY(entry->next != nullptr)) {
                    entry = entry->next;
                    info(", ");
                } else {
                    info("\n");
                    break;
                }
            }
        }
    }

    void check_tcache() {
#ifdef TCA_CHECK
        if (LIKELY(!running_under_2_30_or_later))
            _check_tcache_impl(leak.info->tcache);
        else
            _check_tcache_impl((tcache_perthread_struct_2_30*)leak.info->tcache);
#endif
    }

    template <class TcacheLayout>
    void _check_tcache_impl(TcacheLayout* tcache) {
        static_assert(is_valid_tcache_layout<TcacheLayout>(), "TcacheLayout must be valid tcache_perthread_struct");

        if (UNLIKELY(!isInitialized)) return;
        if (LIKELY(!this->modes.tcaMode)) return;
        if (UNLIKELY(!data.tcacheHasData)) return;

        // Use leaked bin
        if (UNLIKELY(tcache == nullptr)) return;

        // Iterate entries in tcache struct
        for (int i = 0; i < TCACHE_ENTRIES; i++) {
            struct tcache_entry* entryList = tcache->entries[i];
            // TODO: Reconsider usage of tcache->counts because it might be manipulated
            if (UNLIKELY(entryList == nullptr)) continue;
            if (UNLIKELY(tcache->counts[i] <= 0)) continue;

            // Iterate elements in bin
            info(
                "TCA     (CHK ) %p (%d) => %d element(s): \n", tcache, i,
                tcache->counts[i]);
            for (int b = 0; b < TCA_BIN_SIZE; b++) {

                // Validate metadata in hdr by using data from bucket
                // Individual checks for each field
                // If validation fails abort
                CHUNK_HEADER* hdr = CHUNK_HEADER::from_memory(entryList);
                TcacheMetaEntry* bucket = &data.tcache[i][b];

                // the prevsize belongs to the previous chunk and can therefore not be checked here
                // bool psizeValid = (bucket->prev_size == hdr->prev_size) ? true : false;
                if (UNLIKELY(bucket->next != hdr->fd)) {
                    warn("TCA     (CHK ) tcache_bin corrupted: (%p) fd-field not valid\n", entryList);
                    fflush(stderr);
                    raise(SIGILL);
                }
                if (UNLIKELY(bucket->orig_ptr != (void*)entryList)) {
                    warn("TCA     (CHK ) tcache_bin corrupted: (%p) bk-field not valid\n", entryList);
                    fflush(stderr);
                    raise(SIGILL);
                }
                if (UNLIKELY(bucket->size != hdr->chunksize())) {
                    warn("TCA     (CHK ) tcache_bin corrupted: (%p) size-field not valid\n", entryList);
                    fflush(stderr);
                    raise(SIGILL);
                }

                /*
                  info("TCA     (CHK ) Successfully checked %p\n", entryList);
                */

                if (LIKELY(entryList->next != nullptr)) {
                    entryList = entryList->next;
                } else {
                    break;
                }
            }
        }

        // TODO: Check if necessary that memory is zeroed out
        //  CHUNK_HEADER tcache[TCA_ENTRIES_MAX][TCA_BIN_SIZE]
        // int storageSize = sizeof(CHUNK_HEADER) * TCA_ENTRIES_MAX *
        // TCA_BIN_SIZE; memset(data.tcache, '\0', storageSize );
        data.tcacheHasData = false;
    }

    void store_unsorted() {
#ifdef USB_CHECK
        _store_unsorted_impl();
#endif
    }

    void _store_unsorted_impl() {
        if (UNLIKELY(!isInitialized)) return;
        if (!this->modes.usbMode) return;

        data.unsorted_size = 0;
        auto unsorted_start = (CHUNK_HEADER*)*this->leak.info->unsorted_bin;
        auto single = unsorted_start;
        while (true) {
            data.unsorted[data.unsorted_size++] =
                LINKED_LIST_META::from_chunk_header(*single);
            info("USRT    (STR ) Stored unsorted_bin[%d] (%p)\n", data.unsorted_size - 1, single);
            if (UNLIKELY(single->fd == unsorted_start)) break;
            if (UNLIKELY(data.unsorted_size >= USB_ENTRIES_MAX)) break;
            single = single->fd;
        }
    }

    void check_unsorted() {
#ifdef USB_CHECK
        _check_unsorted_impl();
#endif
    }

    void _check_unsorted_impl() {
        if (UNLIKELY(!isInitialized)) return;
        if (!this->modes.usbMode) return;

        auto single = (CHUNK_HEADER*)*this->leak.info->unsorted_bin;
        for (int i = 0; i < data.unsorted_size; i++) {
            auto stored = data.unsorted[i];
            auto actual = LINKED_LIST_META::from_chunk_header(*single);
            if (UNLIKELY(actual != stored)) {
                warn("USRT    (CHK ) Element %d has invalid metadata %p\n", i, single);
                warn(
                    "USRT    (CHK ) stored.ptr=%p  actual.ptr=%p\n", stored.ptr,
                    actual.ptr);
                warn(
                    "USRT    (CHK ) stored.size=%p actual.size=%p\n",
                    stored.chunksize, actual.chunksize);
                warn("unsorted_bin corrupted: (%p) failed\n", single);
                fflush(stderr);
                raise(SIGILL);
            }
            info("USRT    (CHK ) Successfully checked\n");
            single = single->fd;
        }
    }

    void store_topchunk() {
#ifdef TOP_CHECK
        _store_topchunk_impl();
#endif
    }

    void _store_topchunk_impl() {
        if (UNLIKELY(!isInitialized)) return;
        if (UNLIKELY(!this->modes.topMode)) return;
        data.topchunksize = (*this->leak.info->topchunk)->size;
        info("TOPC    (STR ) Stored topchunksize (%p)\n", data.topchunksize);
    }

    void check_topchunk() {
#ifdef TOP_CHECK
        _check_topchunk_impl();
#endif
    }

    void _check_topchunk_impl() {
        if (UNLIKELY(!isInitialized)) return;
        if (UNLIKELY(!this->modes.topMode)) return;
        if (UNLIKELY(data.topchunksize == 0)) return;

        auto expected_topchunk_size = (*this->leak.info->topchunk)->size;
        if (UNLIKELY(data.topchunksize != expected_topchunk_size)) {
            warn("topchunk corrupted: old=%p new=%p\n", data.topchunksize, expected_topchunk_size);
            fflush(stderr);
            raise(SIGILL);
        }
    }

    void store_pointer(size_t len, void* ret) {
#ifdef PTR_CHECK
        if (UNLIKELY(!this->modes.ptrMode)) return;
        auto header = CHUNK_HEADER::from_memory(ret);
        data.store->put(MALLOC_META::from_chunk_header(*header));
#endif
    }

    void check_pointer_before_free(void* ptr) {
        if (UNLIKELY(!this->modes.ptrMode)) return;
        auto header = CHUNK_HEADER::from_memory(ptr);

        // the mmap flag needs to be checked before freeing,
        // because accessing freed mmapped chunks segfaults :)
        // bool is_mmapped = header->is_mmapped();

        MALLOC_META meta = MALLOC_META::from_chunk_header(*header);
        MALLOC_META stored = data.store->get(ptr);

        // Checking flags might lead to situations where a previous chunk was freed
        // and the shadowcopy reflects that its still in use
        // Therefore only size field can be checked
        // bool headerMatchesShadowCopy = (this->modes.leakMode) ?
        //                                   stored.equals_ptr_size_flags(meta) :
        //                                   stored.equals_ptr_size(meta);
        bool headerMatchesShadowCopy = stored.equals_ptr_size(meta);

        if (UNLIKELY(!headerMatchesShadowCopy)) {
            auto prevHeader = header->prev_chunk();
            MALLOC_META prevMetaStore = data.store->get(prevHeader->to_memory());
            warn(
                "FREE    (CHK ) Prev was: %16p sz:%16p ptr:%16p\n",
                prevHeader->to_memory(), prevMetaStore.size, prevMetaStore.ptr);
            warn("FREE    (CHK ) Element has invalid metadata %p\n", ptr);
            warn("FREE    (CHK ) chunkStore.ptr=%p single=%p\n", stored.ptr, ptr);
            warn("FREE    (CHK ) chunkStore.size=%p chunkList.size=%p\n", stored.size, meta.size);
            goto on_error;
        }

        if (UNLIKELY(!data.store->remove(meta))) {
            warn("The pointer (%16p) was not found in Metastore\n", ptr);
            goto on_error;
        }

        info(
            "FREE    (CHK ) Successfully checked pointer (%p) "
            "Prevchunk was: %16p Nextchunk was: %16p\n",
            ptr, header->prev_chunk(), header->next_chunk());

        // Originally freed here, but we must free
        // regardless of whether ptrMode is enabled!
        // this->info.call_free_raw(ptr);

        // Check if prev_in_use flag has changed after call to free
        // (does not change if chunk was put on tcache-list or into
        // fastbinY). If flag has changed update shadow copy,
        // only needed when libcheck is enabled.
        // Note: updating unconditionally is just as expensive as checking whether the update is necessary.
        // The next chunk doesn't exist if this chunk is mmapped!
        // TODO: Consider if update of metadata is required
        // IF_LIKELY(!is_mmapped) {
        //   update_next_chunk_in_storage(header);
        //}
        return;

    on_error:
        warn("free(%p) failed\n", ptr);
        fflush(stderr);
        raise(SIGILL);
    }

    void free_pre(void* ptr) {
        if (NOT_YET_INITIALIZED) return;
        check_topchunk();
        check_unsorted();
        check_tcache();
        trace("FREE    (PRE ) Ptr: %16p\n", ptr);
        check_pointer_before_free(ptr);
    }

    void free_post(void* ptr) {
        if (NOT_YET_INITIALIZED) return;
        trace("FREE    (POST) Ptr: %16p \n", ptr);
        store_tcache();
        store_unsorted();
        store_topchunk();
    }

    void malloc_pre(size_t len) {
        if (NOT_YET_INITIALIZED) return;
        trace("MALLOC  (PRE ) Len: %16zu\n", len, this->leak.isInitialized);
        check_topchunk();
        check_unsorted();
        check_tcache();
    }

    void malloc_post(size_t len, void* ret) {
        if (NOT_YET_INITIALIZED) return;
        if (UNLIKELY(ret == nullptr)) return;
        trace("MALLOC  (POST) Len: %16zu Ret: %16p\n", len, ret);
        // auto header = CHUNK_HEADER::from_memory(ret);
        // update_next_chunk_in_storage(header);
        store_pointer(len, ret);

        // Store pointer can allocate and therefore manipulate state of tcache
        // So only start saving other metadata than pointers from here
        store_tcache();
        store_unsorted();
        store_topchunk();
    }

    void calloc_pre(size_t cnt, size_t len) {
        if (NOT_YET_INITIALIZED) return;
        trace("CALLOC  (PRE ) Cnt: %16zu Len: %16zu\n", cnt, len);
        check_topchunk();
        check_unsorted();
        check_tcache();
    }

    void calloc_post(size_t cnt, size_t len, void* ret) {
        if (NOT_YET_INITIALIZED) return;
        if (UNLIKELY(ret == nullptr)) return;
        trace("CALLOC  (POST) Cnt: %16zu Len: %16zu Ret: %16p\n", cnt, len, ret);
        store_pointer(len, ret);
        store_tcache();
        store_unsorted();
        store_topchunk();
    }

    void realloc_pre(void* ptr, size_t len) {
        if (NOT_YET_INITIALIZED) return;
        trace("REALLOC (PRE ) Start Ptr: %16p Len: %16zu\n", ptr, len);
        check_topchunk();
        check_unsorted();
        check_tcache();
    }

    void realloc_post(void* ptr, size_t len, void* ret) {
        if (NOT_YET_INITIALIZED) return;
        trace("REALLOC (POST) End   Ptr: %16p Len: %16zu Ret: %16p\n", ptr, len, ret);
        store_tcache();
        store_unsorted();
        store_topchunk();
    }

    void realloc_mallochandler(void* ret, size_t len) {
        if (NOT_YET_INITIALIZED) return;
        if (UNLIKELY(ret == nullptr)) return;
        trace("REALLOC (MH  ) Start Ret: %16p Len: %16zu\n", ret, len);
        store_pointer(len, ret);
    }

    void realloc_freehandler(void* ptr) {
        if (NOT_YET_INITIALIZED) return;
        trace("REALLOC (FH  ) Start Ptr: %16p\n", ptr);
        check_pointer_before_free(ptr);
        this->info.call_free_raw(ptr);
    }

    /// Returns true if no update is necessary (e.g. because libcheck is disabled).
    /// Returns true if no next chunk exists (for mmapped chunks).
    /// Returns true if the update to the stored metadata was performed successfully.
    /// Otherwise, returns false, e.g. if no metadata was stored for the next chunk.
    bool update_next_chunk_in_storage(CHUNK_HEADER* chunk) {
#ifdef LEAK_CHECK
        return _update_next_chunk_in_storage_impl(chunk);
#else
        return true;
#endif
    }

    bool _update_next_chunk_in_storage_impl(CHUNK_HEADER* chunk) {
#ifdef PTR_CHECK
        if (NOT_YET_INITIALIZED) return true;
        if (LIKELY(!modes.leakMode)) return true;
        if (UNLIKELY(chunk->is_mmapped())) return true;
        return data.store->update(MALLOC_META::from_chunk_header(*chunk->next_chunk()));
#endif
    }

    /*
        void update_pointer_on_consolidation(int from, int to, void* ptr) {
    #ifdef LEAK_CHECK
            //_update_pointer_on_consolidation_impl(from, to, ptr);
    #endif
        }

        void _update_pointer_on_consolidation_impl(int from, int to, void* ptr) {
            if (NOT_YET_INITIALIZED) return;
            if (!this->modes.leakMode) return;
            if (to < 8) {
                auto header = CHUNK_HEADER::from_memory(ptr);
                auto nextChunk = header->next_chunk();
                auto nextChunkMem = nextChunk->to_memory();
                MALLOC_META meta = MALLOC_META::from_chunk_header(*header);
                MALLOC_META nextMeta = MALLOC_META::from_chunk_header(*nextChunk);
                MALLOC_META nextStored = data.store->get(nextChunkMem);
                if (to == 4 || to == 5) {
                    MALLOC_META nextMetaStore = data.store->get(nextChunkMem);
                    data.store->remove(nextMetaStore);
                    nextMetaStore.size = nextChunk->size;
                    data.store->put(nextMetaStore);
                    debug(
                        "--> Updated Dirty chunk (%d) at %16p (%16p) "
                        "nextMeta.sz:%zu nextStored.sz: %zu\n",
                        to, ptr, nextChunk, nextMeta.size, nextStored.size);
                } else {
                    debug(
                        "Dirty chunk (%d) at %16p (%16p) nextMeta.sz:%zu "
                        "nextStored.sz: %zu\n",
                        to, ptr, nextChunk, nextMeta.size, nextStored.size);
                }
            } else {
                debug("Dirty chunk (%d) at %16p\n", to, ptr);
            }
        }
    */
};  // Class

// inline void handlerCallback(int from, int to, void* ptr) {
//     // IF_UNLIKELY(!facade) return;
//     // if(to != 4 && to != -1){
//     // facade->update_pointer_on_consolidation(from, to, ((char*)ptr) + 0x10);
//     //}
//     /*
//     switch(to){
//         case 4:
//             facade->update_pointer_on_consolidation(from, to, ptr);
//             break;
//         default:
//             break;
//     }
//     */
// }
