#include "util.h"

void *memset(void *dst, int c, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

int memcmp(const void *a, const void *b, uint32_t n) {
    const uint8_t *x = (const uint8_t*)a;
    const uint8_t *y = (const uint8_t*)b;
    while (n--) {
        if (*x != *y) return (int)(*x) - (int)(*y);
        x++; y++;
    }
    return 0;
}

uint32_t strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}