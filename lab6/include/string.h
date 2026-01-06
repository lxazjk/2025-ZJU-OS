#pragma once

#include "defs.h" // 确保能通过它找到 size_t, uint32_t 等定义

int memcmp(const void *v1, const void *v2, size_t n);

size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);