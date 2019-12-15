#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int (*mcmp)(const void *, const void *, size_t);
void *(*mmem)(const void *, size_t, const void *, size_t);

typedef struct _casecmp {
    const char *a;
    const char *b;
    size_t l;
} casecmp;

typedef struct _casemem {
    const char *a;
    size_t al;
    const char *b;
    size_t bl;
} casemem;

void testcmp(int (*mcmp)(const void *, const void *, size_t), const casecmp *cc,
             size_t cl) {
    for (int i = 0; i < cl; i++) {
        int m = mcmp(cc[i].a, cc[i].b, cc[i].l);
        printf("%s %s => %d\n", cc[i].a, cc[i].b, m);
    }
}

void testmem(void *(*mmem)(const void *, size_t, const void *, size_t),
             const casemem *cc, size_t cl) {
    for (int i = 0; i < cl; i++) {
        char *p = (char *)mmem(cc[i].a, cc[i].al, cc[i].b, cc[i].bl);
        printf("%s %s => %s\n", cc[i].a, cc[i].b, p);
    }
}

int main(int argc, char **argv) {
    char p[128];
    const char *pgname = argv[0];
    srandom(time(NULL));
    srand(time(NULL));
    bzero(p, sizeof p);
    strncpy(p, pgname, sizeof p);
    size_t pglen = strlen(pgname);
    const casecmp cc[] = {{p, pgname, sizeof p}, {pgname, pgname, pglen}};
    const casemem cm[] = {{p, sizeof p, pgname, pglen},
                          {pgname, pglen, pgname, pglen},
                          {pgname, pglen, "ug", 2}};

    size_t cclen = sizeof(cc) / sizeof(cc[0]);
    size_t cmlen = sizeof(cm) / sizeof(cm[0]);

    testcmp(memcmp, cc, cclen);
    testcmp(bcmp, cc, cclen);

    testmem(memmem, cm, cmlen);

    long rd = random() % INT_MAX;
    printf("random is %ld\n", rd);
    int rrd = rand() % INT_MAX;
    printf("rand is %d\n", rrd);
    int l = memcmp("abcdefghij", "1234567890", 10);
    printf("%d\n", l);
    l = bcmp("abcdefghij", "1234567890", 10);
    printf("%d\n", l);
    void *ptr = malloc(128);
    free(ptr);
    ptr = memset(p, '1', sizeof(p) - 1);
    printf("%c\n", p[0]);

    return 0;
}
