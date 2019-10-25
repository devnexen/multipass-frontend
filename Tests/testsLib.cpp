#include "libs.h"
#include <assert.h>

int main(int argc, char **argv) {
  int ret = safe_proc_maps(-1);
  assert(ret != -1);
  int index = 0;

  while (pmap[index].s != 0) {
    fprintf(stderr, "%p-%p %" PRIu64 " - huge ? %d (%ld)\n", reinterpret_cast<void *>(pmap[index].s),
            reinterpret_cast<void *>(pmap[index].e), pmap[index].sz, pmap[index].hgmp, pmap[index].f);
    ++index;
  }

  return 0;
}
