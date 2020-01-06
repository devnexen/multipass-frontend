#include "libs.h"
#include <err.h>
#include <string.h>

void generateBzeroCall(void (*bzerofn)(void *, size_t)) {
    char buf[256];
    bzerofn(buf, sizeof(buf));
}

void generateMemsetCall(void *(*memsetfn)(void *, int, size_t)) {
    char buf[256];
    memsetfn(buf, 0, sizeof(buf));

    for (int i = 0; i < sizeof(buf); i++) {
        if ((int)buf[i] != 0)
            errx(1, "At %d, not a %d but %d byte\n", i, 0, (int)buf[i]);
    }
}

void *__attribute__((weak)) memsetToBzeroWrapper(void *b, int c, size_t l) {
    void *(*const volatile memsetw)(void *, int, size_t) = memset;
    return memsetw(b, c, l);
}

int main(int argc, char **argv) {
    generateBzeroCall(bzero);
    generateBzeroCall(explicit_bzero);
    generateBzeroCall(safe_bzero);

    generateMemsetCall(memset);
    generateMemsetCall(memsetToBzeroWrapper);
    generateMemsetCall(safe_memset);

    return 0;
}
