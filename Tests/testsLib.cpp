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
    char p[12];
    char buf[256];
    char *str;
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
    ret = safe_alloc(&ptr, 4096, 1<<21);
    testCond("safe_alloc", ret == 0);
    safe_free(ptr);
    ret = (safe_memmem("ab", 2, "cd", 2) == nullptr);
    testCond("safe_memmem", ret == 1);
    ret = (safe_memmem("abcd", 4, "cd", 2) != nullptr);
    testCond("safe_memmem", ret == 1);
    safe_memset(buf, '1', sizeof(buf) - 1);
    testCond("safe_memset", buf[0] == '1');
    safe_strncpy(p, "abcdefeghijklmnopq", 10);
    testCond("safe_strncpy", !strcmp(p, "abcdefeghi"));
    safe_strncat(p, "def", 10);
    testCond("safe_strncat", !strcmp(p, "abcdefeghi"));
    safe_strcpy(p, "g");
    testCond("safe_strcpy", !strcmp(p, "g"));
    safe_strcat(p, "hur");
    testCond("safe_strcat", !strcmp(p, "ghur"));
    str = safe_strstr(p, "u");
    testCond("safe_strstr", !strcmp(str, "ur"));
    int index = 0;

    while (pmap[index].s != 0) {
        fprintf(stderr, "%p-%p %" PRIu64 " - huge ? %d (%s)\n",
                reinterpret_cast<void *>(pmap[index].s),
                reinterpret_cast<void *>(pmap[index].e),
                static_cast<int64_t>(pmap[index].sz), pmap[index].hgmp,
                pmap[index].fstr);
        ++index;
    }

    ptr = safe_calloc(16, 32);
    safe_free(ptr);

    return 0;
}
