#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>

void *memset(void *dst, int c, uint32_t n);
void *memcpy(void *dst, const void *src, uint32_t n);
int memcmp(const void *a, const void *b, uint32_t n);
uint32_t strlen(const char *s);
char *strcpy(char *dst, const char *src);

#endif