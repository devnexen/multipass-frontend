#include "libs.h"
#include <stdlib.h>
#include <string.h>

extern "C" {
void *malloc(size_t l) { return safe_malloc(l); }

void *realloc(void *o, size_t l) { return safe_realloc(o, l); }

void *calloc(size_t nm, size_t l) { return safe_calloc(nm, l); }

void free(void *ptr) { return safe_free(ptr); }

int rand(void) { return safe_rand(); }

long random(void) { return safe_random(); }

void srand(unsigned seed) { (void)seed; }

void srandom(unsigned int seed) { (void)seed; }

int memcmp(const void *a, const void *b, size_t l) {
    return safe_bcmp(a, b, l);
}

int bcmp(const void *a, const void *b, size_t l) { return safe_bcmp(a, b, l); }

void *memset(void *a, int c, size_t l) { return safe_memset(a, c, l); }

void bzero(void *a, size_t l) { return safe_bzero(a, l); }

void *memmem(const void *h, size_t hl, const void *bh, size_t bhl) {
    return safe_memmem(h, hl, bh, bhl);
}

char *strcpy(char *dst, const char *src) {
    return safe_strcpy(dst, src, strlen(src));
}

char *strncpy(char *dst, const char *src, size_t len) {
    return safe_strcpy(dst, src, len);
}

char *strcat(char *dst, const char *src) {
    return safe_strcat(dst, src, strlen(src));
}

char *strncat(char *dst, const char *src, size_t len) {
    return safe_strcat(dst, src, len);
}
}
