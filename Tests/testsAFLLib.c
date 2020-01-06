#include "libs.h"

static char buf[1024];
static char p[16];
static ssize_t r;

void testWithSz(size_t sz) {
	safe_strncpy(p, buf, sz);
	safe_strncat(p, buf, sz);
	safe_memmem(p, sz, buf, r);
	safe_bcmp(p, buf, sz);
}

int main(int argc, char **argv) {
	r = read(0, buf, sizeof(buf));
	if (r <= 0)
		return 1;
    testWithSz(sizeof(p));
    testWithSz(r);
	return 0;
}
