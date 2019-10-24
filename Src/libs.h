#include <string.h>

extern "C" {
    void safe_bzero(void *, size_t);
    int safe_bcmp(const void *, const void *, size_t);
}
