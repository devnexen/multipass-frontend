#include <assert.h>
#include <string.h>
#if defined(__linux__)
#include <sys/random.h>
#else
#include <unistd.h>
#endif

extern "C" {
void safe_bzero(void *, size_t);
int safe_bcmp(const void *, const void *, size_t);
void safe_random(void *, size_t);
}
