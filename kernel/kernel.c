#include "idt.h"
#include "vga.h"
#include "serial.h"
#include "e820.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "pit.h"
#include "sched.h"
#include "shell.h"

#define VGA_ROWS 25
#define VGA_COLS 80

static void vga_direct_str(int row, int col, uint8_t color, const char *s) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    while (*s)
        vga[row * VGA_COLS + col++] = (uint16_t)((color << 8) | (uint8_t)*s++);
}
static void vga_direct_clr(int row, uint8_t color) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int c = 0; c < VGA_COLS; c++)
        vga[row * VGA_COLS + c] = (uint16_t)(color << 8 | ' ');
}
static void vga_paint_all(uint8_t color) {
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        vga[i] = (uint16_t)(color << 8 | ' ');
}
static void draw_statusbar(void) {
    uint8_t bar = VGA_COLOR(VGA_WHITE,  VGA_DARK_GREY);
    uint8_t clk = VGA_COLOR(VGA_YELLOW, VGA_DARK_GREY);
    vga_direct_clr(23, bar);
    vga_direct_str(23, 0, bar,
        "----------------------------------------"
        "----------------------------------------");
    vga_direct_clr(24, bar);
    vga_direct_str(24,  1, bar, "AxiomX v0.1");
    vga_direct_str(24, 30, bar, "Uptime:");
    vga_direct_str(24, 38, clk, "0s");
    vga_direct_str(24, 52, bar, "| 100 Hz");
}

void kernel_main(void) {
    if (serial_init(COM1) != 0) {
        /* serial failed — continue without it */
    }
    serial_puts("\n\n===========================================\n");
    serial_puts("  AxiomX  |  x86_64 Kernel\n");
    serial_puts("===========================================\n");

    uint8_t base = VGA_COLOR(VGA_WHITE,       VGA_BLUE);
    uint8_t hi   = VGA_COLOR(VGA_YELLOW,      VGA_BLUE);
    uint8_t info = VGA_COLOR(VGA_LIGHT_CYAN,  VGA_BLUE);
    uint8_t good = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLUE);

    vga_init();
    vga_paint_all(base);
    vga_clear(base);

    vga_direct_str( 2, 0, hi,
        "  AxiomX  |  x86_64  |  64-bit Long Mode  |  Kernel v0.1");
    vga_direct_str( 3, 0, info,
        "--------------------------------------------------------");
    vga_direct_str( 4, 2, info,
        "Kernel at 0x100000  |  Stack at 0x200000");

    idt_init();
    vga_direct_str( 5, 2, good, "[ OK ] IDT installed.");
    serial_puts("[kernel] IDT loaded\n");

    e820_print();
    vga_direct_str( 6, 2, good, "[ OK ] E820 memory map read.");
    serial_puts("[kernel] E820 done\n");

    pmm_init();
    vga_direct_str( 7, 2, good, "[ OK ] Physical Memory Manager ready.");
    serial_puts("[kernel] PMM ready\n");

    vmm_init();
    vga_direct_str( 8, 2, good, "[ OK ] Virtual Memory Manager ready.");
    serial_puts("[kernel] VMM ready\n");

    heap_init();
    vga_direct_str( 9, 2, good, "[ OK ] Kernel heap ready.");
    serial_puts("[kernel] Heap ready\n");

    pit_init(100);
    vga_direct_str(10, 2, good, "[ OK ] PIT timer at 100 Hz.");
    serial_puts("[kernel] PIT configured\n");

    sched_init();
    vga_direct_str(11, 2, good, "[ OK ] Scheduler initialised.");
    serial_puts("[kernel] Scheduler ready\n");

    shell_init();
    vga_direct_str(12, 2, good, "[ OK ] Shell initialised.");
    serial_puts("[kernel] Shell ready\n");

    sched_spawn(shell_run, "shell");
    vga_direct_str(13, 2, good, "[ OK ] Shell process spawned.");
    serial_puts("[kernel] Shell spawned\n");

    vga_direct_str(14, 0, info,
        "--------------------------------------------------------");

    vga_set_cursor(15, 0);
    draw_statusbar();

    serial_puts("[kernel] Enabling interrupts\n");
    __asm__ volatile("sti");
    serial_puts("[kernel] Running.\n");
    serial_puts("-------------------------------------------\n");

    while (1) {
        sched_yield();
    }
}