#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int (*mcmp)(const void *, const void *, size_t);

typedef struct _cases {
    const char *a;
    const char *b;
    size_t l;
} cases;

void test(int (*mcmp)(const void *, const void *, size_t), const cases *cc,
          size_t cl) {
    for (int i = 0; i < cl; i++) {
        int m = mcmp(cc[i].a, cc[i].b, cc[i].l);
        printf("%s %s => %d\n", cc[i].a, cc[i].b, m);
    }
}

int main(int argc, char **argv) {
    char p[128];
    const char *pgname = argv[0];
    bzero(p, sizeof p);
    strncpy(p, pgname, sizeof p);
    const cases cc[] = {{p, pgname, sizeof p},
                        {pgname, pgname, strlen(pgname)}};

    test(memcmp, cc, 2);
    test(bcmp, cc, 2);

    return 0;
}
