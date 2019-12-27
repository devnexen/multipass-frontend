#include <assert.h>
#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/mman.h>
#include <sys/random.h>
#elif defined(__FreeBSD__)
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#endif

#if defined(__cplusplus)
extern "C" {
#endif

struct p_proc_map {
    uintptr_t s;
    uintptr_t e;
    size_t sz;
    int hgmp;
    int __reserved;
    int64_t f;
    char res[24];
};

const size_t PROC_MAP_MAX = 256;
const size_t HUGE_MAP_SZ = 2 * 1024 * 1024;
#if defined(USE_MMAP)
const int32_t canary = 0x3aff5d;
const size_t szl = sizeof(size_t);
const size_t cl = sizeof(canary);
#endif

static struct p_proc_map pmap[PROC_MAP_MAX] = {{0}};

void safe_bzero(void *, size_t);
void *safe_memset(void *, int, size_t);
int safe_bcmp(const void *, const void *, size_t);
void *safe_memmem(const void *, size_t, const void *, size_t);
int safe_getrandom(void *, size_t);
int safe_proc_maps(pid_t);
int safe_alloc(void **, size_t, size_t);
void safe_free(void *);
char *safe_strcpy(char *, const char *, size_t);
char *safe_strcat(char *, const char *, size_t);

// Few wrappers
void *safe_malloc(size_t);
void *safe_calloc(size_t, size_t);
void *safe_realloc(void *, size_t);
long safe_random(void);
int safe_rand(void);
#if defined(__cplusplus)
}
#endif
