#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#if defined(__linux__)
#include <linux/mman.h>
#include <sys/random.h>
#elif defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#endif

extern "C" {

struct p_proc_map {
  uintptr_t s;
  uintptr_t e;
  int64_t f;
};

const size_t PROC_MAP_MAX = 256;

__thread struct p_proc_map pmap[PROC_MAP_MAX] = {{0, 0}};

void safe_bzero(void *, size_t);
int safe_bcmp(const void *, const void *, size_t);
void safe_random(void *, size_t);
int safe_proc_maps(pid_t);
}
