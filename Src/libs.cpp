#include "libs.h"

extern "C" {
void safe_bzero(void *p, size_t l) {
  void *(*volatile const b)(void *, int, size_t) = memset;
  (void)b(p, 0, l);
}

int safe_bcmp(const void *a, const void *b, size_t l) {
  const volatile unsigned char *ua =
      reinterpret_cast<const volatile unsigned char *>(a);
  const volatile unsigned char *ub =
      reinterpret_cast<const volatile unsigned char *>(b);
  size_t idx = 0;
  int delta = 0;

  while (idx < l) {
    delta |= ua[idx] ^ ub[idx];
    idx++;
  }

  return delta;
}

void safe_random(void *buf, size_t len) {
#if defined(__linux__)
  ssize_t written = getrandom(buf, len, 0);
  (void)written;
  assert(written == len);
#else
  arc4random_buf(buf, len);
#endif
}

int safe_proc_maps(pid_t pid) {
  int ret = -1;
  if (pid == -1)
    pid = getpid();
#if defined(__linux__)
  char path[256];
  snprintf(path, sizeof(path), "/proc/%d/maps", pid);

  FILE *fp = fopen(path, "r");
  if (fp) {
    char buf[256];
    int index = 0;
    while ((fgets(buf, sizeof(buf), fp))) {
      uintptr_t s;
      uintptr_t e;
      char flag[4];
      sscanf(buf, "%12lx-%12lx %c%c%c%c", &s, &e, &flag[0], &flag[1], &flag[2],
             &flag[3]);

      if (index < PROC_MAP_MAX) {
        int64_t f = 0;
        f |= static_cast<int>(flag[0]);
        f |= static_cast<int>(flag[1]);
        memcpy(&pmap[index].s, &s, sizeof(s));
        memcpy(&pmap[index].e, &e, sizeof(e));
        memcpy(&pmap[index].f, &f, sizeof(f));
        index++;
      } else {
        pmap[index] = {0, 0};
        break;
      }
    }
    ret = 0;
    fclose(fp);
  }
#endif
  return ret;
}
}
