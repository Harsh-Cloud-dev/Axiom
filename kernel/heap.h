#ifndef HEAP_H
#define HEAP_H
#include "types.h"

#define HEAP_START      0x400000ULL
#define HEAP_INITIAL    (4096 * 4)
#define HEAP_MAX        0x800000ULL

typedef struct heap_block {
    size_t            size;
    int               free;
    struct heap_block *next;
    struct heap_block *prev;
    uint32_t          magic;
} heap_block_t;

#define HEAP_MAGIC      0xDEADBEEF
#define HEAP_HDR_SZ     sizeof(heap_block_t)
#define HEAP_MIN_SPLIT  32

void  heap_init(void);
void *kmalloc(size_t size);
void  kfree(void *ptr);
void *kzalloc(size_t size);
void  heap_print_stats(void);
#endif