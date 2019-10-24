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
}
