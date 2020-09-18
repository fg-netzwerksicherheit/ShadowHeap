#include "ShadowHeapWrapper.h"

ShadowHeapWrapper wrapper;

extern "C" void* malloc(size_t len) {
    return wrapper.malloc(len);
}

extern "C" void* calloc(size_t cnt, size_t len) {
    return wrapper.calloc(cnt, len);
}

extern "C" void* realloc(void* ptr, size_t len) {
    return wrapper.realloc(ptr, len);
}

extern "C" void free(void* ptr) {
    wrapper.free(ptr);
}
