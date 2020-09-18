#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define __USE_GNU
#include <dlfcn.h>

static void* (*mallocp)(size_t);
static void* (*callocp)(size_t, size_t);
static void* (*reallocp)(void*, size_t);
static void (*freep)(void*);

static void __attribute__((constructor)) init(void) {
    mallocp = dlsym(RTLD_NEXT, "malloc");
    callocp = dlsym(RTLD_NEXT, "calloc");
    reallocp = dlsym(RTLD_NEXT, "realloc");
    freep = dlsym(RTLD_NEXT, "free");
}

int main(int argc, char** argv) {
    printf(
        "malloc is at: %p\n"
        "calloc is at: %p\n"
        "realloc is at: %p\n"
        "free is at: %p\n",
        mallocp, callocp, reallocp, freep);
    return 0;
}
