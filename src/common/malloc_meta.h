#pragma once
#include "../common/common.h"
#include <cstring>
#include <iostream>

// flags for the size in the chunk header
#define PREV_INUSE 0x1
#define IS_MMAPPED 0x2
#define NON_MAIN_ARENA 0x4
#define SIZE_BITS (PREV_INUSE | IS_MMAPPED | NON_MAIN_ARENA)

/// represent the chunk header from the underlying malloc.
/// May only be used as a reinterpret-view.
struct CHUNK_HEADER {
    /// prev size is only available if the chunk is in freed state!
    size_t prev_size;
    size_t size;
    CHUNK_HEADER* fd;  // only in freed state
    CHUNK_HEADER* bk;  // only in freed state

    // avoid accidental construction
    CHUNK_HEADER(){};
    CHUNK_HEADER(CHUNK_HEADER const&) = delete;

    void* to_memory() {
        auto raw_pointer = reinterpret_cast<char*>(this);
        return reinterpret_cast<void*>(raw_pointer + 2 * sizeof(size_t));
    }

    static CHUNK_HEADER* from_memory(void* ptr) {
        if (UNLIKELY(!ptr)) return nullptr;
        auto raw_ptr = reinterpret_cast<char*>(ptr);
        return reinterpret_cast<CHUNK_HEADER*>(raw_ptr - 2 * sizeof(size_t));
    }

    size_t chunksize() const {
        return size & ~(SIZE_BITS);
    }

    /// The useable size of a chunk is all of the memory
    /// except for the `size` field at the start.
    /// The `prev_size` field logically belongs to the *next* chunk,
    /// but physically to this chunk. It can be used while this chunk is in use,
    /// because malloc's actual overhead is only 1 size_t size, plus alignment padding.
    /// Compare the `request2size()` macro in the glibc malloc source code.
    ///
    /// However, real mmapped chunks have two sizes overhead! The mmap flag may
    /// also be set for “dumped” main arena chunks that have one size overhead,
    /// but checking for those seems impossible (and rare?).
    size_t useable_size() const {
        if (UNLIKELY(is_mmapped())) {
            return chunksize() - 2 * sizeof(size_t);
        } else {
            return chunksize() - sizeof(size_t);
        }
    }

    bool is_prev_inuse() const {
        return size & PREV_INUSE;
    }

    bool is_mmapped() const {
        return size & IS_MMAPPED;
    }

    bool is_main_arena() const {
        return !(size & NON_MAIN_ARENA);
    }

    CHUNK_HEADER* next_chunk() {
        auto raw_pointer = reinterpret_cast<char*>(this);
        return reinterpret_cast<CHUNK_HEADER*>(raw_pointer + chunksize());
    }

    CHUNK_HEADER* prev_chunk() {
        auto raw_pointer = reinterpret_cast<char*>(this);
        return reinterpret_cast<CHUNK_HEADER*>(raw_pointer - prev_size);
    }
};

/// represent stored metadata for doubly linked list entries
struct LINKED_LIST_META {
    void* ptr;
    size_t chunksize;
    CHUNK_HEADER* fd;
    CHUNK_HEADER* bk;

    LINKED_LIST_META() = default;

    static LINKED_LIST_META from_chunk_header(CHUNK_HEADER& header) {
        return LINKED_LIST_META{ header.to_memory(), header.chunksize(),
                                 header.fd, header.bk };
    }

    bool operator==(LINKED_LIST_META& other) {
        return (
            LIKELY(this->ptr == other.ptr) && LIKELY(this->chunksize == other.chunksize) &&
            LIKELY(this->fd == other.fd) && LIKELY(this->bk == other.bk));
    }

    bool operator!=(LINKED_LIST_META& other) {
        return !LIKELY(*this == other);
    }
};

/// represent the metadata format that is used internally
struct MALLOC_META {
    void* ptr;
    size_t size;

    MALLOC_META() = default;

    bool is_some() const {
        return ptr && size;  // check that this isn't the default object
    }

    size_t chunksize() const {
        return size & ~(SIZE_BITS);
    }

    bool is_prev_inuse() const {
        return size & PREV_INUSE;
    }

    bool is_mmapped() const {
        return size & IS_MMAPPED;
    }

    bool is_main_arena() const {
        return !(size & NON_MAIN_ARENA);
    }

    static MALLOC_META from_chunk_header(CHUNK_HEADER& chunk) {
        return { chunk.to_memory(), chunk.size };
    }

    CHUNK_HEADER* to_chunk_header() {
        return CHUNK_HEADER::from_memory(ptr);
    }

    bool matches_chunk(CHUNK_HEADER& header, bool verify_links = false) {
        // must represent the same pointer
        if (UNLIKELY(ptr != header.to_memory())) return false;

        // must have the same chunksize
        if (UNLIKELY(chunksize() != header.chunksize())) return false;

        if (verify_links) {
            // // must have same chunksize in trailer
            if (chunksize() != header.next_chunk()->prev_size) return false;

            // // must match the PREV_INUSE flag
            if (is_prev_inuse() != header.is_prev_inuse()) return false;
        }

        return true;
    }

    bool equals_ptr_size(MALLOC_META other) const {
        return LIKELY(ptr == other.ptr)  // metadata must represent the same pointer
               && LIKELY(chunksize() == other.chunksize());  // logical size must be unchanged
    }

    bool equals_ptr_size_flags(MALLOC_META other) const {
        return LIKELY(ptr == other.ptr)  // metadata must represent the same pointer
               && LIKELY(size == other.size);  // raw size must be exact match incl all flags
    }

    friend bool operator==(MALLOC_META const& left, MALLOC_META const& right) {
        return left.equals_ptr_size(right);
    }

    friend bool operator!=(MALLOC_META const& left, MALLOC_META const& right) {
        return !(left == right);
    }

    friend std::ostream& operator<<(std::ostream& out, MALLOC_META& m) {
        return out << "MALLOC_META {"
                   << " ptr: " << m.ptr << " size: " << m.size << " }";
    }
};
