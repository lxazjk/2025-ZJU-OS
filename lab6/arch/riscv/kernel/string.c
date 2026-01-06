#include "string.h"

int memcmp(const void *v1, const void *v2, size_t n) {
    const unsigned char *s1, *s2;
    s1 = v1;
    s2 = v2;
    while (n-- > 0) {
        if (*s1 != *s2)
            return *s1 - *s2;
        s1++, s2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

char *strcpy(char *dst, const char *src) {
    char *os = dst;
    while ((*dst++ = *src++) != 0)
        ;
    return os;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *os = dst;
    while (n-- > 0 && (*dst++ = *src++) != 0)
        ;
    while (n-- > 0)
        *dst++ = 0;
    return os;
}