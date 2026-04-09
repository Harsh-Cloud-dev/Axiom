#ifndef E820_H
#define E820_H

#include "types.h"

/* ── E820 entry types ────────────────────────────────────────────────────── */
#define E820_USABLE       1   /* free RAM                          */
#define E820_RESERVED     2   /* hardware reserved, do not use     */
#define E820_ACPI_RECLAIM 3   /* ACPI tables, reclaimable after    */
#define E820_ACPI_NVS     4   /* ACPI non-volatile storage         */
#define E820_BAD          5   /* bad memory, do not use            */

/* ── Single E820 entry (24 bytes, as BIOS returns it) ───────────────────── */
typedef struct {
    uint64_t base;          /* start physical address             */
    uint64_t length;        /* length in bytes                    */
    uint32_t type;          /* region type (see defines above)    */
    uint32_t acpi_ext;      /* ACPI 3.0 extended attributes       */
} __attribute__((packed)) e820_entry_t;

/* ── Map location written by Stage1 ─────────────────────────────────────── */
#define E820_MAP_ADDR    ((void *)0x500)
#define E820_COUNT_ADDR  ((uint16_t *)0x500)
#define E820_ENTRIES     ((e820_entry_t *)0x504)

/* ── Public API ──────────────────────────────────────────────────────────── */

/* Print the full E820 map to serial */
void e820_print(void);

/* Return total usable RAM in bytes */
uint64_t e820_total_usable(void);

/* Return the number of entries */
uint16_t e820_count(void);

#endif