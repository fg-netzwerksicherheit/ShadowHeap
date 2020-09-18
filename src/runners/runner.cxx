#include "../facade/ShadowHeapFacade.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* malloc(size_t sz);
void free(void* mem);

int main(int arc, char** argv) {
    int testsize = 0x100;
    int tcache_size = 7;

    void* fillers[tcache_size];
    for (int i = 0; i < tcache_size; i++) {
        fillers[i] = malloc(testsize);
    }
    printf("-----------------------------------\n");
    void* ptr1 = malloc(testsize);
    void* arr = calloc(10, testsize);
    void* re = malloc(testsize);
    re = realloc(re, testsize * 2);
    printf("-----------------------------------\n");
    for (int i = 0; i < tcache_size; i++) {
        free(fillers[i]);
    }
    printf("-----------------------------------\n");

    // malloc / free
    memset(ptr1, 0x41, testsize);
    free(ptr1);
    void* ptr2 = malloc(testsize);
    void* ptr3 = malloc(testsize);

    // calloc / free
    free(arr);

    // realloc / free
    free(re);

    printf("Allocations successful\n");
    return 0;
}
