#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdbool.h>

typedef unsigned char  uchar;
typedef unsigned short ushort;
typedef unsigned int   uint;
typedef unsigned long  ulong;

#define ERROR(fmt, ...) do { \
    fprintf(stderr, "[\x1b[31mERROR\x1b[0m] " fmt "\n", ##__VA_ARGS__); \
    exit(1); \
} while (0)
#define LOG(fmt, ...) fprintf(stderr, "[\x1b[96mINFO\x1b[0m] " fmt "\n", ##__VA_ARGS__)
#define WARN(fmt, ...) fprintf(stderr, "[\x1b[33mWARNING\x1b[0m] " fmt "\n", ##__VA_ARGS__)

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#endif
