#include "libs.h"

int main(int argc, char **argv) {
	char buf[1024];

	ssize_t r = read(0, buf, sizeof(buf));
	if (r <= 0)
		return 1;
	char p[16];
	safe_strcpy(p, buf, sizeof(p));
	safe_strcat(p, buf, sizeof(p));
	safe_memmem(p, sizeof(p), buf, r);
	safe_bcmp(p, buf, sizeof(p));
	return 0;
}
