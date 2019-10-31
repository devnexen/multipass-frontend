#include <assert.h>
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

extern "C" {

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

__thread struct p_proc_map pmap[PROC_MAP_MAX] = {{0}};

void safe_bzero(void *, size_t);
int safe_bcmp(const void *, const void *, size_t);
int safe_random(void *, size_t);
int safe_proc_maps(pid_t);
}
