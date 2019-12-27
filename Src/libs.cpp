#include "libs.h"

extern "C" {

const int CLOBBER = 0xdead;

#if !defined(USE_MMAP)
static void (*ofree)(void *) = nullptr;

void init_libc(void) {
    if (ofree)
        return;

    ofree = reinterpret_cast<decltype(ofree)>(dlsym(RTLD_NEXT, "free"));
    if (!ofree)
        errx(1, "%s\n", dlerror());
}
#endif

void safe_bzero(void *p, size_t l) { (void)safe_memset(p, 0, l); }

void *safe_memset(void *p, int c, size_t l) {
    volatile char *ptr = reinterpret_cast<volatile char *>(p);
    for (auto i = 0ul; i < l; i++)
        ptr[i] = c;

    return p;
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

void *safe_memmem(const void *a, size_t al, const void *b, size_t bl) {
    const unsigned char *ua = reinterpret_cast<const unsigned char *>(a);
    const unsigned char *ub = reinterpret_cast<const unsigned char *>(b);

    if (bl == 0)
        return const_cast<void *>(a);
    if (al < bl)
        return nullptr;

    if (bl == 1)
        return memchr(const_cast<void *>(a), *ub, bl);

    const volatile unsigned char *end = ua + (al - bl);

    for (const unsigned char *cur = ua; cur <= end; ++cur) {
        unsigned char *pcur = const_cast<unsigned char *>(cur);
        void *vcur = reinterpret_cast<void *>(pcur);
        if (cur[0] == ub[0] && safe_bcmp(vcur, b, bl) == 0)
            return vcur;
    }

    return nullptr;
}

int safe_getrandom(void *buf, size_t len) {
    safe_bzero(buf, len);
#if defined(__linux__)
    ssize_t written = getrandom(buf, len, 0);
    (void)written;
    assert(written == static_cast<ssize_t>(len));
    return (written == static_cast<ssize_t>(len) ? 0 : -1);
#else
    arc4random_buf(buf, len);
    return 0;
#endif
}

int safe_proc_maps(pid_t pid) {
    int ret = -1;
    int saved_err = errno;
    size_t index = 0;
    errno = 0;
    if (pid == -1)
        pid = getpid();
#if defined(__linux__)
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);

    FILE *fp = fopen(path, "r");
    if (fp) {
        char buf[256];
        while ((fgets(buf, sizeof(buf), fp))) {
            uintptr_t s;
            uintptr_t e;
            char flag[4];
            sscanf(buf, "%12lx-%12lx %c%c%c%c", &s, &e, &flag[0], &flag[1],
                   &flag[2], &flag[3]);

            if (index < PROC_MAP_MAX) {
                int64_t f = 0;
                f |= static_cast<int>(flag[0]);
                f |= static_cast<int>(flag[1]);
                size_t tsz = (e - s);
                memcpy(&pmap[index].s, &s, sizeof(s));
                memcpy(&pmap[index].e, &e, sizeof(e));
                memcpy(&pmap[index].f, &f, sizeof(f));
                memcpy(&pmap[index].sz, &tsz, sizeof(tsz));
                pmap[index].hgmp = (tsz >= HUGE_MAP_SZ);
                index++;
            } else {
                break;
            }
        }
        ret = 0;
        fclose(fp);
    }
#elif defined(__FreeBSD__)
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_VMMAP, pid};
    size_t miblen = sizeof(mib) / sizeof(mib[0]);
    size_t len;
    char *b, *s, *e;

    if (sysctl(mib, miblen, nullptr, &len, nullptr, 0) == -1)
        return -1;
    len = len * 4 / 3;
    b = reinterpret_cast<char *>(mmap(nullptr, len, PROT_READ | PROT_WRITE,
                                      MAP_SHARED | MAP_ANON, -1, 0));
    if (b != (void *)-1) {
        if (sysctl(mib, miblen, b, &len, nullptr, 0) == -1) {
            munmap(b, len);
            return -1;
        }

        s = b;
        e = s + len;

        while (s < e) {
            struct kinfo_vmentry *e = (struct kinfo_vmentry *)s;
            size_t sz = e->kve_structsize;

            if (sz == 0)
                break;

            int64_t f = 0;
            if (e->kve_protection & KVME_PROT_READ)
                f |= KVME_PROT_READ;
            if (e->kve_protection & KVME_PROT_WRITE)
                f |= KVME_PROT_WRITE;
            if (e->kve_protection & KVME_PROT_EXEC)
                f |= KVME_PROT_EXEC;
            size_t tsz = (e->kve_end - e->kve_start);
            memcpy(&pmap[index].s, &e->kve_start, sizeof(pmap[index].s));
            memcpy(&pmap[index].e, &e->kve_end, sizeof(pmap[index].e));
            memcpy(&pmap[index].f, &f, sizeof(pmap[index].f));
            memcpy(&pmap[index].sz, &tsz, sizeof(pmap[index].sz));
            pmap[index].hgmp = (tsz >= HUGE_MAP_SZ);
            index++;

            s += sz;
        }

        ret = 0;
        munmap(b, len);
    }
#else
    (void)index;
    errno = ENOSYS;
    return 0;
#endif
    errno = saved_err;
    return ret;
}

int safe_alloc(void **ptr, size_t a, size_t l) {
    if (!ptr)
        return -1;
    errno = 0;
#if defined(USE_MMAP)
    size_t tl = cl + szl + l;
    int mflags = MAP_SHARED | MAP_ANON;
#if defined(__FreeBSD__)
    mflags |= MAP_ALIGNED(12);
#endif
    *ptr = mmap(nullptr, tl, PROT_READ | PROT_WRITE, mflags, -1, 0);
    if (*ptr == MAP_FAILED) {
        *ptr = nullptr;
        return -1;
    }
    auto p = reinterpret_cast<char *>(*ptr);
    ::memcpy(p, &canary, cl);
    p += cl;
    ::memcpy(p, &l, szl);
    p += szl;
    *ptr = p;
    return 0;
#else
    void *p;
    int r = posix_memalign(&p, a, l);
    *ptr = p;
    return r;
#endif
}

void safe_free(void *ptr) {
    errno = 0;
#if defined(USE_MMAP)
    if (!ptr)
        return;
    size_t l;
    int32_t readc;
    auto p = reinterpret_cast<char *>(ptr);
    p -= szl;
    ::memcpy(&l, p, szl);
    p -= cl;
    ::memcpy(&readc, p, cl);
    if (readc != canary)
        errno = EINVAL;
    safe_memset(p, CLOBBER, szl);
    munmap(p, szl + cl + l);
#else
    init_libc();
    ofree(ptr);
#endif
}

char *safe_strcpy(char *dst, const char *src, size_t l) {
    int d = 0;
    char *udst = dst;
    const char *usrc = src;

    if (!dst || !src)
        return NULL;

    while (--l > 0) {
        if (!*usrc || !(*udst++ = *usrc++))
            break;
        ++d;
    }

    dst[d] = 0;

    return dst;
}

char *safe_strcat(char *dst, const char *src, size_t l) {
    char *udst = dst;

    if (l == 0)
        return dst;

    while (udst && *udst && --l > 0)
        ++udst;

    (void)safe_strcpy(udst, src, l);
    return dst;
}

void *safe_malloc(size_t l) {
    void *ptr;
    safe_alloc(&ptr, 16, l);

    if (ptr)
        safe_memset(ptr, CLOBBER, l);

    return ptr;
}

void *safe_calloc(size_t nm, size_t l) {
    void *ptr;
    ptr = safe_malloc(nm * l);

    if (ptr)
        safe_memset(ptr, 0, nm * l);

    return ptr;
}

void *safe_realloc(void *o, size_t l) {
    void *ptr;
    ptr = safe_malloc(l);

    if (!ptr)
        return nullptr;

    safe_free(o);
    o = nullptr;
    return ptr;
}

long safe_random(void) {
    long ret;
    safe_getrandom(&ret, sizeof(ret));

    return ret;
}

int safe_rand(void) {
    int ret;
    safe_getrandom(&ret, sizeof(ret));

    return ret;
}
}
