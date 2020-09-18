#pragma once

#define USB_ENTRIES_MAX 128
#define TCA_ENTRIES_MAX 64
#define TCA_BIN_SIZE 7

#define NBINS 128
#define NFASTBINS 10
#define BINMAPSHIFT 5
#define BITSPERMAP (1U << BINMAPSHIFT)
#define BINMAPSIZE (NBINS / BITSPERMAP)
#define TCACHE_ENTRIES 64
#define GLIBC_LEN_VERSION 100
#define PROGNAME_LEN 100
#define TEST_SIZE_LEAK 8
#define TEST_SIZE_TCACHEMALLOC 0x1
#define TEST_SIZE_BARRIER 0x1000
#define TEST_SIZE_TCACHEBIN 7


#define warn(fmt, ...)                                                         \
    do {                                                                       \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                   \
        fflush(stderr);                                                        \
    } while (0)

#undef debug
#undef info
#undef trace

#ifdef VERBOSE
#define debug(fmt, ...)                                                        \
    do {                                                                       \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                   \
        fflush(stderr);                                                        \
    } while (0)


#ifdef INFO_MSG
#define info(fmt, ...)                                                         \
    do {                                                                       \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                   \
        fflush(stderr);                                                        \
    } while (0)
#else
#define info(fmt, ...)
#endif

#ifdef TRACE_MSG
#define trace(fmt, ...)                                                        \
    do {                                                                       \
        fprintf(stderr, fmt, ##__VA_ARGS__);                                   \
        fflush(stderr);                                                        \
    } while (0)
#else
#define trace(fmt, ...)
#endif
#endif

#ifndef debug
#define debug(fmt, ...)
#endif

#ifndef trace
#define trace(fmt, ...)
#endif

#ifndef info
#define info(fmt, ...)
#endif

#define SPECULATE(expr, value) __builtin_expect(expr, value)
#define LIKELY(expr) SPECULATE(expr, 1)
#define UNLIKELY(expr) SPECULATE(expr, 0)
#define IF_LIKELY(expr) if (SPECULATE((expr), 1))
#define IF_UNLIKELY(expr) if (SPECULATE((expr), 0))
