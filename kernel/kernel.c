#include "idt.h"
#include "vga.h"
#include "serial.h"

void kernel_main(void) {
    /* Serial first — works even if VGA is broken */
    if (serial_init(COM1) == 0) {
        serial_puts("\n\n");
        serial_puts("===========================================\n");
        serial_puts("  MyOS  |  Serial Console Active (COM1)\n");
        serial_puts("===========================================\n");
        serial_printf("  Kernel at : %p\n", (void *)0x100000);
        serial_printf("  COM1 port : 0x%x\n", (uint64_t)COM1);
        serial_puts("-------------------------------------------\n");
    }

    /* VGA */
    uint8_t base = VGA_COLOR(VGA_WHITE,       VGA_BLUE);
    uint8_t hi   = VGA_COLOR(VGA_YELLOW,      VGA_BLUE);
    uint8_t info = VGA_COLOR(VGA_LIGHT_CYAN,  VGA_BLUE);
    uint8_t good = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLUE);

    vga_clear(base);
    vga_puts_at(0, 0, hi,   "  MyOS  |  x86_64  |  64-bit Long Mode  |  Kernel v0.1");
    vga_puts_at(1, 0, info, "--------------------------------------------------------");
    vga_puts_at(2, 2, info, "Kernel loaded at : 0x100000");
    vga_puts_at(3, 2, info, "Mode             : 64-bit Long Mode");
    vga_puts_at(4, 2, info, "Boot chain       : S0 > S1 > S2 > S3 > Kernel");
    serial_puts("[kernel] VGA ready\n");

    /* IDT */
    idt_init();
    serial_puts("[kernel] IDT installed, interrupts enabled\n");

    vga_puts_at(5, 2, good, "[ OK ] IDT installed. Interrupts enabled.");
    vga_puts_at(6, 2, good, "[ OK ] Serial output on COM1  (115200 8N1).");
    vga_puts_at(7, 0, info, "--------------------------------------------------------");
    vga_puts_at(8, 0, info, "");

    serial_puts("[kernel] Ready.\n");
    serial_puts("-------------------------------------------\n");
    serial_puts("  Keyboard input will appear below:\n");
    serial_puts("-------------------------------------------\n");

    while (1) __asm__ volatile("hlt");
}