#include "heap.h"
#include "vmm.h"
#include "pmm.h"
#include "serial.h"

static heap_block_t *heap_head = (heap_block_t *)HEAP_START;
static uint64_t      heap_end  = HEAP_START;

static int heap_extend(uint64_t bytes) {
    uint64_t pages = (bytes + 4095) / 4096;

    if (heap_end + pages * 4096 > HEAP_MAX) {
        serial_puts("[heap] ERROR: heap limit reached\n");
        return -1;
    }

    for (uint64_t i = 0; i < pages; i++) {
        uint64_t phys = pmm_alloc();
        if (phys == PMM_ALLOC_FAIL) {
            serial_puts("[heap] ERROR: pmm_alloc failed during extend\n");
            return -1;
        }
        /*
         * heap region 0x400000-0x800000 is already identity-mapped
         * by vmm_init's 128MB huge page mapping.
         * We only need to track logical heap_end — no vmm_map needed.
         * Calling vmm_map here would corrupt the heap by treating
         * the heap address as a page table pointer.
         */
        (void)phys;
        heap_end += 4096;
    }

    return 0;
}

static void heap_split(heap_block_t *block, size_t size) {
    if (block->size < size + HEAP_HDR_SZ + HEAP_MIN_SPLIT)
        return;

    heap_block_t *new_block = (heap_block_t *)((uint8_t *)block + HEAP_HDR_SZ + size);
    new_block->size  = block->size - size - HEAP_HDR_SZ;
    new_block->free  = 1;
    new_block->magic = HEAP_MAGIC;
    new_block->next  = block->next;
    new_block->prev  = block;

    if (block->next)
        block->next->prev = new_block;

    block->next = new_block;
    block->size = size;
}

static void heap_coalesce(heap_block_t *block) {
    while (block->next && block->next->free) {
        block->size += HEAP_HDR_SZ + block->next->size;
        block->next  = block->next->next;
        if (block->next)
            block->next->prev = block;
    }

    if (block->prev && block->prev->free) {
        block->prev->size += HEAP_HDR_SZ + block->size;
        block->prev->next  = block->next;
        if (block->next)
            block->next->prev = block->prev;
    }
}

void heap_init(void) {
    if (heap_extend(HEAP_INITIAL) < 0) {
        serial_puts("[heap] FATAL: cannot map initial heap\n");
        __asm__ volatile("cli; hlt");
    }

    heap_head->size  = HEAP_INITIAL - HEAP_HDR_SZ;
    heap_head->free  = 1;
    heap_head->next  = (void *)0;
    heap_head->prev  = (void *)0;
    heap_head->magic = HEAP_MAGIC;

    serial_printf("[heap] Initialised at 0x%x, %u bytes\n",
                  (unsigned int)HEAP_START, (unsigned int)HEAP_INITIAL);
}

void *kmalloc(size_t size) {
    if (size == 0) return (void *)0;

    size = (size + 7) & ~(size_t)7;

    heap_block_t *b = heap_head;
    while (b) {
        if (b->magic != HEAP_MAGIC) {
            serial_puts("[heap] CORRUPTION detected in kmalloc\n");
            return (void *)0;
        }
        if (b->free && b->size >= size) {
            heap_split(b, size);
            b->free = 0;
            return (void *)((uint8_t *)b + HEAP_HDR_SZ);
        }
        b = b->next;
    }

    uint64_t needed = size + HEAP_HDR_SZ;
    if (heap_extend(needed) < 0)
        return (void *)0;

    heap_block_t *last = heap_head;
    while (last->next) last = last->next;

    if (last->free) {
        last->size += needed;
        heap_split(last, size);
        last->free = 0;
        return (void *)((uint8_t *)last + HEAP_HDR_SZ);
    } else {
        heap_block_t *new_block = (heap_block_t *)((uint8_t *)last + HEAP_HDR_SZ + last->size);
        new_block->size  = needed - HEAP_HDR_SZ;
        new_block->free  = 0;
        new_block->next  = (void *)0;
        new_block->prev  = last;
        new_block->magic = HEAP_MAGIC;
        last->next = new_block;
        heap_split(new_block, size);
        return (void *)((uint8_t *)new_block + HEAP_HDR_SZ);
    }
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - HEAP_HDR_SZ);

    if (block->magic != HEAP_MAGIC) {
        serial_printf("[heap] kfree: bad magic at %p\n", ptr);
        return;
    }

    if (block->free) {
        serial_printf("[heap] kfree: double-free at %p\n", ptr);
        return;
    }

    block->free = 1;
    heap_coalesce(block);
}

void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (!ptr) return (void *)0;

    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < size; i++)
        p[i] = 0;

    return ptr;
}

void heap_print_stats(void) {
    heap_block_t *b     = heap_head;
    uint64_t      total = 0;
    uint64_t      free  = 0;
    uint64_t      used  = 0;
    int           count = 0;

    serial_puts("\n[heap] Block List:\n");
    serial_puts("  Addr               Size       Status\n");

    while (b) {
        if (b->magic != HEAP_MAGIC) {
            serial_puts("  [CORRUPT BLOCK DETECTED]\n");
            break;
        }

        serial_puts("  ");
        serial_puthex((uint64_t)b);
        serial_printf("  %u bytes  ", (unsigned int)b->size);
        serial_puts(b->free ? "FREE\n" : "USED\n");

        total += b->size;
        if (b->free) free  += b->size;
        else         used  += b->size;
        count++;
        b = b->next;
    }

    serial_puts("[heap] ────────────────────────────────────\n");
    serial_printf("  Blocks : %u\n",       (unsigned int)count);
    serial_printf("  Total  : %u bytes\n", (unsigned int)total);
    serial_printf("  Used   : %u bytes\n", (unsigned int)used);
    serial_printf("  Free   : %u bytes\n", (unsigned int)free);
    serial_puts("[heap] ────────────────────────────────────\n\n");
}