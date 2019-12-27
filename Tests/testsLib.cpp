#include "libs.h"
#include <assert.h>

void testCond(const char *name, bool cond) {
    fprintf(stderr, "%s: ", name);
    if (errno == ENOSYS) {
        fprintf(stderr, "Not implemented\n");
    } else {
        assert(cond);
        fprintf(stderr, "Test passed\n");
    }
}

int main(int argc, char **argv) {
    int ret = -1;
    char p[10];
    char buf[256];
    void *ptr;
    safe_bzero(p, sizeof(p));
    testCond("safe_bzero", p[0] == 0);
    ret = safe_getrandom(buf, sizeof(buf));
    testCond("safe_getrandom", ret == 0);
    ret = safe_bcmp("a", "a", 1);
    testCond("safe_bcmp", ret == 0);
    ret = safe_bcmp("a", "b", 1);
    testCond("safe_bcmp", ret != 0);
    ret = safe_proc_maps(-1);
    testCond("safe_proc_maps", ret != -1);
    ret = safe_alloc(&ptr, 4096, 16);
    testCond("safe_alloc", ret == 0);
    safe_free(ptr);
    ret = (safe_memmem("ab", 2, "cd", 2) == nullptr);
    testCond("safe_memmem", ret == 1);
    ret = (safe_memmem("abcd", 4, "cd", 2) != nullptr);
    testCond("safe_memmem", ret == 1);
    safe_memset(buf, '1', sizeof(buf) - 1);
    testCond("safe_memset", buf[0] == '1');
    safe_strcpy(p, "abcdefeghijklmnopq", 3);
    testCond("safe_strcpy", !strcmp(p, "ab"));
    safe_strcat(p, "cdef", sizeof(p));
    testCond("safe_strcat", !strcmp(p, "abcdef"));
    int index = 0;

    while (pmap[index].s != 0) {
        fprintf(stderr, "%p-%p %" PRIu64 " - huge ? %d (%" PRIu64 ")\n",
                reinterpret_cast<void *>(pmap[index].s),
                reinterpret_cast<void *>(pmap[index].e),
                static_cast<int64_t>(pmap[index].sz), pmap[index].hgmp,
                pmap[index].f);
        ++index;
    }

    ptr = safe_calloc(16, 32);
    safe_free(ptr);

    return 0;
}
