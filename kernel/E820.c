#include "e820.h"
#include "serial.h"

/* ── Type name lookup ────────────────────────────────────────────────────── */
static const char *type_name(uint32_t type) {
    switch (type) {
        case E820_USABLE:       return "Usable RAM";
        case E820_RESERVED:     return "Reserved";
        case E820_ACPI_RECLAIM: return "ACPI Reclaimable";
        case E820_ACPI_NVS:     return "ACPI NVS";
        case E820_BAD:          return "Bad Memory";
        default:                return "Unknown";
    }
}

uint16_t e820_count(void) {
    return *E820_COUNT_ADDR;
}

void e820_print(void) {
    uint16_t count = e820_count();
    e820_entry_t *entries = E820_ENTRIES;

    serial_printf("\n[e820] Memory map (%u entries):\n", (uint64_t)count);
    serial_puts("  #   Base                 Length               Type\n");
    serial_puts("  --  -------------------  -------------------  ----------------\n");

    for (uint16_t i = 0; i < count; i++) {
        serial_printf("  %u   ", (uint64_t)i);
        serial_puthex(entries[i].base);
        serial_puts("   ");
        serial_puthex(entries[i].length);
        serial_puts("   ");
        serial_puts(type_name(entries[i].type));
        serial_puts("\n");
    }

    serial_printf("[e820] Total usable RAM: ");
    serial_puthex(e820_total_usable());
    serial_puts(" bytes\n\n");
}

uint64_t e820_total_usable(void) {
    uint16_t count = e820_count();
    e820_entry_t *entries = E820_ENTRIES;
    uint64_t total = 0;

    for (uint16_t i = 0; i < count; i++)
        if (entries[i].type == E820_USABLE)
            total += entries[i].length;

    return total;
}