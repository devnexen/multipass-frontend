#include "libs.h"
#include <stdlib.h>
#include <string.h>

extern "C" {
void *malloc(size_t l) {
    return safe_malloc(l);
}

void *realloc(void *o, size_t l) {
    return safe_realloc(o, l);
}

void *calloc(size_t nm, size_t l) {
    return safe_calloc(nm, l);
}

void free(void *ptr) {
    return safe_free(ptr);
}

}
