#include "libs.h"
#include <assert.h>

void testCond(bool cond) {
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
  safe_bzero(p, sizeof(p));
  testCond(p[0] == 0);
  ret = safe_random(buf, sizeof(buf));
  testCond(ret == 0);
  ret = safe_bcmp("a", "a", 1);
  testCond(ret == 0);
  ret = safe_bcmp("a", "b", 1);
  testCond(ret != 0);
  ret = 0;
  ret = safe_proc_maps(-1);
  testCond(ret != -1);
  int index = 0;

  while (pmap[index].s != 0) {
    fprintf(stderr, "%p-%p %" PRIu64 " - huge ? %d (%" PRIu64 ")\n",
            reinterpret_cast<void *>(pmap[index].s),
            reinterpret_cast<void *>(pmap[index].e),
            static_cast<int64_t>(pmap[index].sz), pmap[index].hgmp,
            pmap[index].f);
    ++index;
  }

  return 0;
}
