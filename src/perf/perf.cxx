#include <malloc.h>
#include <random>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

double test_malloc(int size, int amount) {
    double result = 0;
    clock_t start, end;
    void* ptr[amount];
    start = clock();
    for (int i = 0; i < amount; i++) {
        ptr[i] = malloc(size);
    }
    end = clock();
    for (int i = 0; i < amount; i++) {
        free(ptr[i]);
    }
    result = ((double)(end - start)) / CLOCKS_PER_SEC;
    return result;
}

double test_malloc_free(int size, int amount) {
    double result = 0;
    clock_t start, end;
    start = clock();
    for (int i = 0; i < amount; i++) {
        void* ptr = malloc(size);
        free(ptr);
    }
    end = clock();
    result = ((double)(end - start)) / CLOCKS_PER_SEC;
    return result;
}

double test_malloc_memset_free(int size, int amount) {
    double result = 0;
    clock_t start, end;
    start = clock();
    for (int i = 0; i < amount; i++) {
        void* ptr = malloc(size);
        memset(ptr, 0x41, 100);
        free(ptr);
    }
    end = clock();
    result = ((double)(end - start)) / CLOCKS_PER_SEC;
    return result;
}

double test_malloc_free_stochastically(int size, int amount) {
    static std::mt19937 rng(384329);
    std::uniform_int_distribution<size_t> size_distribution(size / 2, size * 2);
    std::bernoulli_distribution free_distribution(0.3);

    size_t ptrsize = 0;
    void* ptr[amount];

    for (int i = 0; i < amount; i++) {
        ptr[ptrsize++] = malloc(size_distribution(rng));

        if (free_distribution(rng)) {
            auto evict = std::uniform_int_distribution<size_t>(0, ptrsize - 1)(rng);
            std::swap(ptr[--ptrsize], ptr[evict]);
            free(ptr[ptrsize]);
        }
    }

    // afterwards, free all remaining elements
    while (ptrsize > 0) {
        free(ptr[--ptrsize]);
    }

    return 0.0;
}

// we don't use the real pow() function from math.h
// so that we have fewer libc dependencies.
static double mypow(double base, int exponent) {
    double result = 1;
    for (int i = 0; i < exponent; i++)
        result += base;
    return result;
}

void collect_all_static(int iterations, int testcount, double results[]) {
    const double factor = 2;  // 10 is too slow right now

    for (int x = 0; x < iterations; x++) {
        // Test malloc only
        for (int i = 0; i < 5; i++) {
            double delta = test_malloc(100, 10000 * mypow(factor, i));
            results[i] += delta;
        }

        // Test malloc and free
        for (int i = 0; i < 5; i++) {
            double delta = test_malloc_free(100, 10000 * mypow(factor, i));
            results[5 + i] += delta;
        }

        // Test malloc memset and free
        for (int i = 0; i < 5; i++) {
            double delta = test_malloc_memset_free(100, 10000 * mypow(factor, i));
            results[10 + i] += delta;
        }

        // Test stochastic malloc/free
        for (int i = 0; i < 5; i++)
            test_malloc_free_stochastically(100, 10000 * mypow(factor, i));
    }

    for (int i = 0; i < testcount; i++) {
        results[i] = results[i] / iterations;
    }
}

void print_results(char* name, int testcount, double results[]) {
    printf("%s", name);
    for (int i = 0; i < testcount; i++) {
        printf(",%f", results[i]);
    }
    printf("\n");
}

int main(int argc, char** argv) {
    char* name = argv[1];
    int testcount = 15;
    int iterations = 10;
    // getchar();

    double results_static[testcount];
    memset(results_static, 0x0, testcount * sizeof(double));
    collect_all_static(iterations, testcount, results_static);
    print_results(name, testcount, results_static);

    return 0;
}
