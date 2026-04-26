#include "shell.h"
#include "vga.h"
#include "serial.h"
#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "pit.h"
#include "sched.h"
#include "e820.h"

static shell_cmd_t cmd_table[SHELL_MAX_CMDS];
static int         cmd_count   = 0;
static int         shell_ready = 0;

static char input_buf[SHELL_MAX_INPUT];
static int  input_len = 0;

#define HISTORY_SIZE 16
static char history[HISTORY_SIZE][SHELL_MAX_INPUT];
static int  history_count = 0;
static int  history_head  = 0;
static int  history_pos   = -1;

static int shift_held = 0;

#define COL_PROMPT  VGA_COLOR(VGA_YELLOW,      VGA_BLUE)
#define COL_OUTPUT  VGA_COLOR(VGA_WHITE,       VGA_BLUE)
#define COL_ERROR   VGA_COLOR(VGA_LIGHT_RED,   VGA_BLUE)
#define COL_SUCCESS VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLUE)
#define COL_INFO    VGA_COLOR(VGA_LIGHT_CYAN,  VGA_BLUE)
#define COL_KEY     VGA_COLOR(VGA_YELLOW,      VGA_BLUE)
#define COL_DIM     VGA_COLOR(VGA_LIGHT_GREY,  VGA_BLUE)

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0,%1"::"a"(val),"Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port));
    return v;
}

#define VGA_CTRL 0x3D4
#define VGA_DATA 0x3D5

static void hw_cursor_enable(void) {
    outb(VGA_CTRL, 0x0A);
    outb(VGA_DATA, (uint8_t)((inb(VGA_DATA) & 0xC0) | 13));
    outb(VGA_CTRL, 0x0B);
    outb(VGA_DATA, (uint8_t)((inb(VGA_DATA) & 0xE0) | 15));
}

static void hw_cursor_move(int row, int col) {
    uint16_t pos = (uint16_t)(row * 80 + col);
    outb(VGA_CTRL, 0x0F); outb(VGA_DATA, (uint8_t)(pos & 0xFF));
    outb(VGA_CTRL, 0x0E); outb(VGA_DATA, (uint8_t)((pos >> 8) & 0xFF));
}

static int kstrlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}
static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static int kstrncmp(const char *a, const char *b, int n) {
    while (n-- > 0 && *a && *a == *b) { a++; b++; }
    return n < 0 ? 0 : (unsigned char)*a - (unsigned char)*b;
}
static void kstrcpy(char *d, const char *s) {
    while ((*d++ = *s++));
}

static void shell_putc(char c, uint8_t col) { vga_putc(c, col); serial_putc(c); }
static void shell_puts(const char *s, uint8_t col) { vga_puts(s, col); serial_puts(s); }

static void shell_putuint(uint64_t v, uint8_t col) {
    char b[24]; int i = 0;
    if (!v) { shell_putc('0', col); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) shell_putc(b[j], col);
}

static void shell_puthex(uint64_t v, uint8_t col) {
    const char *d = "0123456789abcdef";
    shell_puts("0x", col);
    for (int i = 60; i >= 0; i -= 4) shell_putc(d[(v >> i) & 0xF], col);
}

static void shell_puts_safe(const char *s, uint8_t col, int max) {
    int n = 0; while (*s && n < max) { shell_putc(*s++, col); n++; }
}

static void update_hw_cursor(void) {
    int r, c; vga_get_cursor(&r, &c); hw_cursor_move(r, c);
}

static void print_prompt(void) {
    shell_putc('\n', COL_OUTPUT);
    shell_puts(SHELL_PROMPT, COL_PROMPT);
    update_hw_cursor();
}

static void redraw_input(void) {
    int row, col; vga_get_cursor(&row, &col);
    int pl = kstrlen(SHELL_PROMPT); vga_set_cursor(row, pl);
    uint8_t base = VGA_COLOR(VGA_WHITE, VGA_BLUE);
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int c = pl; c < 80; c++)
        vga[row * 80 + c] = (uint16_t)(base << 8 | ' ');
    for (int i = 0; i < input_len; i++) vga_putc(input_buf[i], COL_OUTPUT);
    update_hw_cursor();
}

static void history_push(const char *l) {
    if (!kstrlen(l)) return;
    kstrcpy(history[history_head], l);
    history_head = (history_head + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE) history_count++;
    history_pos = -1;
}

static const char *history_get(int o) {
    if (o < 1 || o > history_count) return (void *)0;
    return history[(history_head - o + HISTORY_SIZE) % HISTORY_SIZE];
}

static int tokenise(char *line, char **argv) {
    int argc = 0; char *p = line;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        if (argc >= SHELL_MAX_ARGS) break;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

static int64_t parse_int(const char *s) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    int64_t v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        while (*s) {
            char c = *s++; int8_t n;
            if      (c >= '0' && c <= '9') n = (int8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') n = (int8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') n = (int8_t)(c - 'A' + 10);
            else break;
            v = (v << 4) | n;
        }
    } else {
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++) - '0';
    }
    return neg ? -v : v;
}

static void print_size(uint64_t b, uint8_t col) {
    if      (b >= 1024*1024*1024) { shell_putuint(b / (1024*1024*1024), col); shell_puts(" GB", col); }
    else if (b >= 1024*1024)      { shell_putuint(b / (1024*1024),      col); shell_puts(" MB", col); }
    else if (b >= 1024)           { shell_putuint(b / 1024,             col); shell_puts(" KB", col); }
    else                          { shell_putuint(b,                    col); shell_puts(" B",  col); }
}

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\n  Commands:\n", COL_INFO);
    shell_puts("  ----------\n", COL_DIM);
    for (int i = 0; i < cmd_count; i++) {
        shell_puts("  ", COL_OUTPUT);
        shell_puts(cmd_table[i].name, COL_KEY);
        int nl = kstrlen(cmd_table[i].name);
        for (int p = nl; p < 10; p++) shell_putc(' ', COL_OUTPUT);
        shell_puts("  ", COL_OUTPUT);
        shell_puts_safe(cmd_table[i].desc, COL_DIM, 52);
        shell_putc('\n', COL_OUTPUT);
    }
    shell_puts("  TAB=complete  UP/DOWN=history\n", COL_DIM);
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    uint8_t base = VGA_COLOR(VGA_WHITE, VGA_BLUE);
    volatile uint16_t *vga = (volatile uint16_t *)0xB8000;
    for (int r = 0; r < 23; r++)
        for (int c = 0; c < 80; c++)
            vga[r * 80 + c] = (uint16_t)(base << 8 | ' ');
    vga_set_cursor(0, 0);
}

static void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t hz = pit_hz();
    uint64_t t  = hz ? pit_ticks() / hz : 0;
    uint64_t h  = t / 3600, m = (t % 3600) / 60, s = t % 60;
    shell_puts("\n  Uptime : ", COL_INFO);
    shell_putuint(h, COL_OUTPUT); shell_puts("h ", COL_OUTPUT);
    shell_putuint(m, COL_OUTPUT); shell_puts("m ", COL_OUTPUT);
    shell_putuint(s, COL_OUTPUT); shell_puts("s\n", COL_OUTPUT);
    shell_puts("  Ticks  : ", COL_INFO);
    shell_putuint(pit_ticks(), COL_OUTPUT);
    shell_puts(" @ ", COL_OUTPUT);
    shell_putuint(hz, COL_OUTPUT);
    shell_puts(" Hz\n", COL_OUTPUT);
}

static void cmd_mem(int argc, char **argv) {
    (void)argc; (void)argv;
    uint64_t total = pmm_total_count(), free = pmm_free_count(), used = total - free;
    shell_puts("\n  Physical Memory:\n", COL_INFO);
    shell_puts("    Total : ", COL_OUTPUT); print_size(total * 4096, COL_SUCCESS);
    shell_puts("  (", COL_DIM); shell_putuint(total, COL_DIM); shell_puts(" pages)\n", COL_DIM);
    shell_puts("    Used  : ", COL_OUTPUT); print_size(used * 4096, COL_ERROR);
    shell_puts("  (", COL_DIM); shell_putuint(used,  COL_DIM); shell_puts(" pages)\n", COL_DIM);
    shell_puts("    Free  : ", COL_OUTPUT); print_size(free * 4096, COL_SUCCESS);
    shell_puts("  (", COL_DIM); shell_putuint(free,  COL_DIM); shell_puts(" pages)\n", COL_DIM);
    shell_puts("\n  Usage [", COL_INFO);
    int bars = (int)((used * 40) / total);
    for (int i = 0; i < 40; i++)
        shell_putc(i < bars ? '#' : '.', i < bars ? COL_ERROR : COL_DIM);
    shell_puts("] ", COL_INFO);
    shell_putuint((used * 100) / total, COL_OUTPUT);
    shell_puts("%\n", COL_OUTPUT);
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    sched_reap();
    shell_putc('\n', COL_OUTPUT);
    sched_print();
}

static void cmd_echo(int argc, char **argv) {
    shell_putc('\n', COL_OUTPUT);
    for (int i = 1; i < argc; i++) {
        shell_puts(argv[i], COL_OUTPUT);
        if (i < argc - 1) shell_putc(' ', COL_OUTPUT);
    }
    shell_putc('\n', COL_OUTPUT);
}

static void cmd_vmmap(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\n  Virtual Memory:\n", COL_INFO);
    shell_puts("    PML4   : ", COL_OUTPUT);
    shell_puthex(vmm_current_pml4(), COL_SUCCESS);
    shell_putc('\n', COL_OUTPUT);
    shell_puts("    0x000000-0x0FFFFF  BIOS/stages\n",    COL_OUTPUT);
    shell_puts("    0x100000-0x1FFFFF  Kernel + stack\n", COL_OUTPUT);
    shell_puts("    0x300000-0x31FFFF  PMM bitmap\n",     COL_OUTPUT);
    shell_puts("    0x400000-0x7FFFFF  Heap + stacks\n",  COL_OUTPUT);
}

static void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\n  Axiom Kernel v0.1\n", COL_INFO);
    shell_puts("  Arch : x86_64 64-bit Long Mode\n",     COL_OUTPUT);
    shell_puts("  Boot : BIOS 4-stage bootloader\n",     COL_OUTPUT);
    shell_puts("  CC   : x86_64-elf-gcc freestanding\n", COL_OUTPUT);
    shell_puts("  ASM  : NASM\n",                        COL_OUTPUT);
    shell_puts("  EMU  : QEMU\n",                        COL_OUTPUT);
}

static void cmd_history(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!history_count) { shell_puts("\n  No history.\n", COL_DIM); return; }
    shell_puts("\n  History:\n", COL_INFO);
    for (int i = history_count; i >= 1; i--) {
        const char *e = history_get(i);
        if (!e) continue;
        shell_puts("    ", COL_OUTPUT);
        shell_putuint((uint64_t)(history_count - i + 1), COL_DIM);
        shell_puts("  ", COL_OUTPUT);
        shell_puts(e, COL_OUTPUT);
        shell_putc('\n', COL_OUTPUT);
    }
}

static void cmd_cpuinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t ebx, ecx, edx;
    __asm__ volatile("cpuid":"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(0));
    char vendor[13];
    vendor[0]  = (char)((ebx >>  0) & 0xFF); vendor[1]  = (char)((ebx >>  8) & 0xFF);
    vendor[2]  = (char)((ebx >> 16) & 0xFF); vendor[3]  = (char)((ebx >> 24) & 0xFF);
    vendor[4]  = (char)((edx >>  0) & 0xFF); vendor[5]  = (char)((edx >>  8) & 0xFF);
    vendor[6]  = (char)((edx >> 16) & 0xFF); vendor[7]  = (char)((edx >> 24) & 0xFF);
    vendor[8]  = (char)((ecx >>  0) & 0xFF); vendor[9]  = (char)((ecx >>  8) & 0xFF);
    vendor[10] = (char)((ecx >> 16) & 0xFF); vendor[11] = (char)((ecx >> 24) & 0xFF);
    vendor[12] = '\0';
    char brand[49]; uint32_t regs[4]; int off = 0;
    for (uint32_t leaf = 0x80000002; leaf <= 0x80000004; leaf++) {
        __asm__ volatile("cpuid":"=a"(regs[0]),"=b"(regs[1]),"=c"(regs[2]),"=d"(regs[3]):"a"(leaf));
        for (int i = 0; i < 4; i++) {
            brand[off++] = (char)((regs[i] >>  0) & 0xFF);
            brand[off++] = (char)((regs[i] >>  8) & 0xFF);
            brand[off++] = (char)((regs[i] >> 16) & 0xFF);
            brand[off++] = (char)((regs[i] >> 24) & 0xFF);
        }
    }
    brand[48] = '\0';
    uint64_t cr3;
    __asm__ volatile("mov %%cr3,%0":"=r"(cr3));
    shell_puts("\n  CPU:\n", COL_INFO);
    shell_puts("    Vendor : ", COL_OUTPUT); shell_puts(vendor, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("    Brand  : ", COL_OUTPUT); shell_puts(brand,  COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("    Mode   : 64-bit Long Mode\n", COL_OUTPUT);
    shell_puts("    CR3    : ", COL_OUTPUT); shell_puthex(cr3, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
}

static void cmd_hexdump(int argc, char **argv) {
    if (argc < 2) { shell_puts("\n  Usage: hexdump <addr> [lines]\n", COL_ERROR); return; }
    const char *p = argv[1];
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    uint64_t addr = 0;
    while (*p) {
        char c = *p++; uint8_t n;
        if      (c >= '0' && c <= '9') n = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') n = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') n = (uint8_t)(c - 'A' + 10);
        else break;
        addr = (addr << 4) | n;
    }
    int lines = 4;
    if (argc >= 3) {
        lines = 0; const char *q = argv[2];
        while (*q >= '0' && *q <= '9') lines = lines * 10 + (*q++) - '0';
        if (lines < 1) lines = 1;
        if (lines > 16) lines = 16;
    }
    shell_putc('\n', COL_OUTPUT);
    const char *hex = "0123456789abcdef";
    for (int line = 0; line < lines; line++) {
        uint64_t ra = addr + (uint64_t)(line * 16);
        shell_puts("  ", COL_DIM);
        for (int i = 28; i >= 0; i -= 4) shell_putc(hex[(ra >> i) & 0xF], COL_DIM);
        shell_puts(": ", COL_DIM);
        volatile uint8_t *mem = (volatile uint8_t *)ra;
        for (int i = 0; i < 16; i++) {
            shell_putc(hex[(mem[i] >> 4) & 0xF], COL_OUTPUT);
            shell_putc(hex[mem[i] & 0xF],        COL_OUTPUT);
            shell_putc(' ', COL_OUTPUT);
            if (i == 7) shell_putc(' ', COL_OUTPUT);
        }
        shell_puts(" |", COL_DIM);
        for (int i = 0; i < 16; i++) {
            char c = (char)mem[i];
            shell_putc((c >= 32 && c < 127) ? c : '.',
                       (c >= 32 && c < 127) ? COL_SUCCESS : COL_DIM);
        }
        shell_puts("|\n", COL_DIM);
    }
}

static void cmd_color(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\n  VGA Colors:\n\n", COL_INFO);
    const char *names[] = {
        "BLACK","BLUE","GREEN","CYAN","RED","MAGENTA","BROWN","LT_GREY",
        "DK_GREY","LT_BLUE","LT_GREEN","LT_CYAN","LT_RED","PINK","YELLOW","WHITE"
    };
    for (int i = 0; i < 16; i++) {
        uint8_t col = VGA_COLOR(i, VGA_BLUE);
        uint8_t blk = VGA_COLOR(VGA_WHITE, (uint8_t)i);
        shell_puts("  ", COL_OUTPUT);
        shell_putc(' ', blk); shell_putc(' ', blk);
        shell_putc(' ', COL_OUTPUT);
        shell_puts(names[i], col);
        shell_putc('\n', COL_OUTPUT);
    }
}

static void cmd_lsmem(int argc, char **argv) {
    (void)argc; (void)argv;
    uint16_t count = e820_count();
    if (count > 64) count = 64;
    volatile e820_entry_t *entries = (volatile e820_entry_t *)0x504;
    const char   *tn[] = { "?", "Usable", "Reserved", "ACPI-R", "ACPI-NVS", "Bad" };
    const uint8_t tc[] = { COL_DIM, COL_SUCCESS, COL_ERROR, COL_INFO, COL_INFO, COL_ERROR };
    shell_puts("\n  E820 Map (", COL_INFO);
    shell_putuint((uint64_t)count, COL_OUTPUT);
    shell_puts(" entries):\n", COL_INFO);
    shell_puts("  Base               Length             Type\n", COL_DIM);
    shell_puts("  -----------------  -----------------  -----------\n", COL_DIM);
    for (uint16_t i = 0; i < count; i++) {
        uint32_t type = entries[i].type;
        if (type > 5) type = 0;
        shell_puts("  ", COL_DIM);
        shell_puthex(entries[i].base,   COL_OUTPUT);
        shell_puts("  ", COL_DIM);
        shell_puthex(entries[i].length, COL_OUTPUT);
        shell_puts("  ", COL_DIM);
        shell_puts(tn[type], tc[type]);
        shell_puts(" (", COL_DIM);
        print_size(entries[i].length, COL_SUCCESS);
        shell_puts(")\n", COL_DIM);
    }
    uint64_t tu = 0, tr = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (entries[i].type == 1) tu += entries[i].length;
        else                      tr += entries[i].length;
    }
    shell_puts("\n  Usable  : ", COL_OUTPUT); print_size(tu, COL_SUCCESS);
    shell_puts("\n  Reserved: ", COL_OUTPUT); print_size(tr, COL_ERROR);
    shell_putc('\n', COL_OUTPUT);
}

static void cmd_memtest(int argc, char **argv) {
    int np = 8;
    if (argc >= 2) {
        np = 0; const char *q = argv[1];
        while (*q >= '0' && *q <= '9') np = np * 10 + (*q++) - '0';
        if (np < 1) np = 1;
        if (np > 64) np = 64;
    }
    shell_puts("\n  Memtest: ", COL_INFO);
    shell_putuint((uint64_t)np, COL_OUTPUT);
    shell_puts(" pages\n", COL_INFO);

    static const uint64_t pats[] = {
        0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL,
        0xFFFFFFFFFFFFFFFFULL, 0x0000000000000000ULL,
        0xDEADBEEFCAFEBABEULL
    };

    uint64_t addrs[64]; int ok = 1;
    for (int i = 0; i < np; i++) {
        addrs[i] = pmm_alloc();
        if (addrs[i] == PMM_ALLOC_FAIL) {
            shell_puts("  FAIL alloc\n", COL_ERROR);
            np = i; ok = 0; break;
        }
    }
    if (ok) shell_puts("  Alloc  : PASS\n", COL_SUCCESS);

    int pass = 1;
    for (int p = 0; p < 5 && pass; p++) {
        uint64_t pat = pats[p];
        for (int i = 0; i < np; i++) {
            volatile uint64_t *m = (volatile uint64_t *)addrs[i];
            for (int w = 0; w < 512; w++) m[w] = pat;
        }
        for (int i = 0; i < np && pass; i++) {
            volatile uint64_t *m = (volatile uint64_t *)addrs[i];
            for (int w = 0; w < 512; w++)
                if (m[w] != pat) { pass = 0; break; }
        }
        if (pass) {
            shell_puts("  Pattern ", COL_OUTPUT);
            shell_puthex(pat, COL_DIM);
            shell_puts(": PASS\n", COL_SUCCESS);
        }
    }
    for (int i = 0; i < np; i++) pmm_free(addrs[i]);
    shell_puts(pass ? "  ALL PASSED\n" : "  FAILED\n",
               pass ? COL_SUCCESS : COL_ERROR);
}

static volatile uint64_t stress_counters[8];
static volatile int      stress_done[8];
static volatile int      stress_slot_counter = 0;

static void stress_worker(void) {
    int slot = stress_slot_counter++;
    if (slot >= 8) slot = 7;
    for (uint64_t k = 0; k < 50000; k++) {
        stress_counters[slot]++;
        if (k % 5000 == 0) sched_yield();
    }
    stress_done[slot] = 1;
}

static void cmd_stress(int argc, char **argv) {
    int np = 3;
    if (argc >= 2) {
        np = 0; const char *q = argv[1];
        while (*q >= '0' && *q <= '9') np = np * 10 + (*q++) - '0';
        if (np < 1) np = 1;
        if (np > 8) np = 8;
    }
    for (int i = 0; i < 8; i++) { stress_counters[i] = 0; stress_done[i] = 0; }
    stress_slot_counter = 0;

    shell_puts("\n  Stress: ", COL_INFO);
    shell_putuint((uint64_t)np, COL_OUTPUT);
    shell_puts(" workers (50k iters each)...\n", COL_INFO);

    static const char *wn[] = { "sw0","sw1","sw2","sw3","sw4","sw5","sw6","sw7" };
    for (int i = 0; i < np; i++) sched_spawn(stress_worker, wn[i]);

    uint64_t start   = pit_ticks();
    uint64_t timeout = (uint64_t)pit_hz() * 30;

    while ((pit_ticks() - start) < timeout) {
        int done = 1;
        for (int i = 0; i < np; i++)
            if (!stress_done[i]) { done = 0; break; }
        if (done) break;
        sched_yield();
    }

    shell_puts("\n  Results:\n", COL_INFO);
    uint64_t total = 0;
    for (int i = 0; i < np; i++) {
        shell_puts("    worker", COL_OUTPUT);
        shell_putuint((uint64_t)i, COL_OUTPUT);
        shell_puts(" : ", COL_OUTPUT);
        shell_putuint(stress_counters[i], COL_SUCCESS);
        shell_puts(" iters\n", COL_OUTPUT);
        total += stress_counters[i];
    }
    shell_puts("    total  : ", COL_INFO);
    shell_putuint(total, COL_SUCCESS);
    shell_puts("\n", COL_OUTPUT);

    int timed_out = (pit_ticks() - start) >= timeout;
    shell_puts(timed_out ? "  Timed out\n" : "  Round-robin confirmed\n",
               timed_out ? COL_ERROR : COL_SUCCESS);

    sched_reap();
}

static void cmd_calc(int argc, char **argv) {
    if (argc < 3) { shell_puts("\n  Usage: calc <a> <op> <b>\n", COL_ERROR); return; }
    shell_putc('\n', COL_OUTPUT);
    if (argv[1][0] == '~' && !argv[1][1]) {
        int64_t a = parse_int(argv[2]); int64_t r = ~a;
        shell_puts("  ~", COL_KEY); shell_puthex((uint64_t)a, COL_OUTPUT);
        shell_puts(" = ", COL_KEY); shell_puthex((uint64_t)r, COL_SUCCESS);
        shell_putc('\n', COL_OUTPUT); return;
    }
    if (argc < 4) { shell_puts("  Need: calc <a> <op> <b>\n", COL_ERROR); return; }
    int64_t a = parse_int(argv[1]); const char *op = argv[2]; int64_t b = parse_int(argv[3]);
    int64_t res = 0; int valid = 1;
    if      (op[0] == '+' && !op[1]) res = a + b;
    else if (op[0] == '-' && !op[1]) res = a - b;
    else if (op[0] == '*' && !op[1]) res = a * b;
    else if (op[0] == '/' && !op[1]) { if (!b) { shell_puts("  Div/0\n", COL_ERROR); return; } res = a / b; }
    else if (op[0] == '%' && !op[1]) { if (!b) { shell_puts("  Mod/0\n", COL_ERROR); return; } res = a % b; }
    else if (op[0] == '&' && !op[1]) res = a & b;
    else if (op[0] == '|' && !op[1]) res = a | b;
    else if (op[0] == '^' && !op[1]) res = a ^ b;
    else if (op[0] == '<' && op[1] == '<' && !op[2]) res = a << (b & 63);
    else if (op[0] == '>' && op[1] == '>' && !op[2]) res = (int64_t)((uint64_t)a >> (b & 63));
    else { shell_puts("  Unknown op\n", COL_ERROR); valid = 0; }
    if (valid) {
        if (a < 0) { shell_putc('-', COL_OUTPUT); shell_putuint((uint64_t)(-a), COL_OUTPUT); }
        else shell_putuint((uint64_t)a, COL_OUTPUT);
        shell_putc(' ', COL_OUTPUT); shell_puts(op, COL_KEY); shell_putc(' ', COL_OUTPUT);
        if (b < 0) { shell_putc('-', COL_OUTPUT); shell_putuint((uint64_t)(-b), COL_OUTPUT); }
        else shell_putuint((uint64_t)b, COL_OUTPUT);
        shell_puts(" = ", COL_KEY);
        if (res < 0) { shell_putc('-', COL_SUCCESS); shell_putuint((uint64_t)(-res), COL_SUCCESS); }
        else shell_putuint((uint64_t)res, COL_SUCCESS);
        shell_puts("  (0x", COL_DIM); shell_puthex((uint64_t)res, COL_SUCCESS); shell_puts(")\n", COL_DIM);
    }
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\nRebooting...\n", COL_ERROR);
    pit_sleep(300);
    outb(0xCF9, 0x06); pit_sleep(50);
    int t = 10000; while ((inb(0x64) & 0x02) && t--);
    outb(0x64, 0xFE); pit_sleep(50);
    __asm__ volatile("cli");
    uint64_t null_idtr = 0;
    __asm__ volatile("lidt (%0)"::"r"(&null_idtr):"memory");
    __asm__ volatile("int $3");
    while (1) __asm__ volatile("hlt");
}

static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    shell_puts("\nHalted.\n", COL_ERROR);
    __asm__ volatile("cli; hlt");
}

static void shell_tab_complete(void) {
    if (!input_len) return;
    int mc = 0, lm = -1;
    for (int i = 0; i < cmd_count; i++)
        if (kstrncmp(input_buf, cmd_table[i].name, input_len) == 0) { mc++; lm = i; }
    if (!mc) return;
    if (mc == 1) {
        const char *n = cmd_table[lm].name; int nl = kstrlen(n);
        for (int i = input_len; i < nl; i++) {
            input_buf[i] = n[i]; vga_putc(n[i], COL_OUTPUT); serial_putc(n[i]);
        }
        input_len = nl; input_buf[input_len] = '\0'; update_hw_cursor(); return;
    }
    shell_putc('\n', COL_OUTPUT);
    for (int i = 0; i < cmd_count; i++)
        if (kstrncmp(input_buf, cmd_table[i].name, input_len) == 0) {
            shell_puts("  ", COL_OUTPUT);
            shell_puts(cmd_table[i].name, COL_KEY);
            shell_putc('\n', COL_OUTPUT);
        }
    shell_puts(SHELL_PROMPT, COL_PROMPT);
    shell_puts(input_buf, COL_OUTPUT);
    update_hw_cursor();
}
#include "sched.h"
#include "heap.h"
#include "serial.h"
#include "vga.h"
#include "pit.h"

#define SCHED_QUANTUM  10

static pcb_t   *current          = (void *)0;
static pcb_t   *queue            = (void *)0;
static uint64_t next_pid         = 0;
static uint64_t ticks_remaining  = SCHED_QUANTUM;
static int      sched_ready      = 0;
static volatile int needs_reschedule = 0;

static switch_entry_t switchlog_buf[SWITCHLOG_SIZE];
static int            switchlog_head   = 0;
static int            switchlog_count  = 0;
static volatile int   switchlog_active = 0;

static uint8_t sched_color(void) {
    return VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLUE);
}

static void sched_puts(const char *s) {
    vga_puts(s, sched_color());
    serial_puts(s);
}

static void sched_putuint(uint64_t v) {
    char b[24]; int i = 0;
    if (!v) { vga_putc('0', sched_color()); serial_putc('0'); return; }
    while (v) { b[i++] = '0' + (v % 10); v /= 10; }
    for (int j = i - 1; j >= 0; j--) {
        vga_putc(b[j], sched_color());
        serial_putc(b[j]);
    }
}

static void sched_puthex(uint64_t v) {
    const char *d = "0123456789abcdef";
    sched_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        char c = d[(v >> i) & 0xF];
        vga_putc(c, sched_color());
        serial_putc(c);
    }
}

static void kstrncpy(char *dst, const char *src, int n) {
    int i = 0;
    while (i < n - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int kstreq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void proc_entry(void) {
    void (*fn)(void) = (void (*)(void))current->stack_top;
    __asm__ volatile("sti");
    fn();

    sched_puts("[sched] process '");
    sched_puts(current->name);
    sched_puts("' (pid ");
    sched_putuint(current->pid);
    sched_puts(") exited\n");

    current->state = PROC_DEAD;
    sched_yield();

    while (1) __asm__ volatile("hlt");
}


/* ── stack ───────────────────────────────────────────────────────────────── */
static void cmd_stack(int argc, char **argv) {
    int lines = 16;
    if (argc >= 2) {
        lines = 0; const char *q = argv[1];
        while (*q >= '0' && *q <= '9') lines = lines * 10 + (*q++) - '0';
        if (lines < 1)  lines = 1;
        if (lines > 64) lines = 64;
    }

    uint64_t rsp, rbp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));

    /*
     * Detect which stack region RSP is in:
     *   0x200000 area  = kernel main stack (entry.asm)
     *   0x400000+ area = process heap-allocated stack
     * Top = nearest page boundary above RSP within known regions
     */
    uint64_t stack_top;
    const char *stack_region;

    if (rsp >= 0x100000 && rsp < 0x200000) {
        stack_top    = 0x200000;
        stack_region = "kernel main stack";
    } else if (rsp >= 0x200000 && rsp < 0x400000) {
        stack_top    = 0x200000;
        stack_region = "kernel main stack";
    } else if (rsp >= 0x400000 && rsp < 0x800000) {
        /* process stack — find top from PCB */
        pcb_t *cur = sched_current();
        if (cur && cur->stack_base && cur->stack_size) {
            stack_top    = cur->stack_base + cur->stack_size;
            stack_region = "process stack (heap)";
        } else {
            stack_top    = (rsp + 0x4000) & ~(uint64_t)0x3FFF;
            stack_region = "process stack (estimated)";
        }
    } else {
        stack_top    = (rsp + 0x4000) & ~(uint64_t)0x3FFF;
        stack_region = "unknown region";
    }

    uint64_t used = (stack_top > rsp) ? (stack_top - rsp) : 0;

    shell_puts("\n  Stack Dump:\n", COL_INFO);
    shell_puts("  ---------------------------------------------\n", COL_DIM);
    shell_puts("  Region : ", COL_OUTPUT); shell_puts(stack_region, COL_KEY); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RSP    : ", COL_OUTPUT); shell_puthex(rsp,       COL_KEY); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RBP    : ", COL_OUTPUT); shell_puthex(rbp,       COL_KEY); shell_putc('\n', COL_OUTPUT);
    shell_puts("  Top    : ", COL_OUTPUT); shell_puthex(stack_top, COL_DIM); shell_putc('\n', COL_OUTPUT);
    shell_puts("  Used   : ", COL_OUTPUT); shell_putuint(used, COL_SUCCESS);
    shell_puts(" bytes\n", COL_OUTPUT);
    shell_puts("  ---------------------------------------------\n", COL_DIM);
    shell_puts("  Address              Value\n", COL_INFO);
    shell_puts("  ---------------------------------------------\n", COL_DIM);

    uint64_t *sp = (uint64_t *)rsp;
    for (int i = 0; i < lines; i++) {
        uint64_t addr = rsp + (uint64_t)(i * 8);
        uint64_t val  = sp[i];

        uint8_t col = COL_OUTPUT;
        if (addr == rsp) col = COL_SUCCESS;
        if (addr == rbp) col = COL_KEY;

        shell_puts("  ", COL_DIM);
        shell_puthex(addr, col);
        shell_puts("  ", COL_DIM);
        shell_puthex(val,  col);

        if (addr == rsp) shell_puts("  <- RSP", COL_SUCCESS);
        if (addr == rbp) shell_puts("  <- RBP (frame pointer)", COL_KEY);
        if (val  == rbp && addr != rbp) shell_puts("  <- saved RBP", COL_DIM);
        shell_putc('\n', COL_OUTPUT);
    }

    /* walk call frames */
    shell_puts("  ---------------------------------------------\n", COL_DIM);
    shell_puts("  Call Frame Chain:\n", COL_INFO);
    uint64_t frame = rbp;
    int      depth = 0;
    while (frame >= rsp && frame < stack_top && depth < 8) {
        uint64_t saved_rbp = *(uint64_t *)frame;
        uint64_t ret_addr  = *(uint64_t *)(frame + 8);
        shell_puts("  frame ", COL_DIM);
        shell_putuint((uint64_t)depth, COL_DIM);
        shell_puts("  rbp=", COL_DIM);
        shell_puthex(frame, COL_KEY);
        shell_puts("  ret=", COL_DIM);
        shell_puthex(ret_addr, COL_SUCCESS);
        shell_putc('\n', COL_OUTPUT);
        if (saved_rbp <= frame) break;
        frame = saved_rbp;
        depth++;
    }
}

/* ── trace ───────────────────────────────────────────────────────────────── */

/* trace uses a simple global flag that other subsystems check */
static volatile int trace_enabled = 0;

int shell_trace_active(void) { return trace_enabled; }

static void cmd_trace(int argc, char **argv) {
    if (argc < 2) {
        shell_puts("\n  Usage: trace on | trace off\n", COL_ERROR);
        shell_puts("  Current: ", COL_OUTPUT);
        shell_puts(trace_enabled ? "ON\n" : "OFF\n",
                   trace_enabled ? COL_SUCCESS : COL_DIM);
        return;
    }

    /* check argv[1] == "on" or "off" */
    const char *arg = argv[1];
    int is_on  = (arg[0]=='o' && arg[1]=='n'  && !arg[2]);
    int is_off = (arg[0]=='o' && arg[1]=='f'  && arg[2]=='f' && !arg[3]);

    if (is_on) {
        trace_enabled = 1;
        shell_puts("\n  Trace ON - kernel events will print in real time.\n", COL_SUCCESS);
        shell_puts("  Events: sched switches, IRQ ticks, mem alloc/free\n", COL_DIM);
        shell_puts("  Type 'trace off' to stop.\n", COL_DIM);
        sched_switchlog_clear();
        sched_switchlog_enable();
    } else if (is_off) {
        trace_enabled = 0;
        sched_switchlog_disable();
        shell_puts("\n  Trace OFF.\n", COL_DIM);
    } else {
        shell_puts("\n  Usage: trace on | trace off\n", COL_ERROR);
    }
}

/* ── schedviz ────────────────────────────────────────────────────────────── */
static void cmd_schedviz(int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t hz        = pit_hz();
    uint64_t tr        = sched_ticks_remaining();
    uint64_t quantum   = 10;
    uint64_t ms_left   = hz ? (tr * 1000) / hz : 0;
    uint64_t ms_total  = hz ? (quantum * 1000) / hz : 0;

    uint8_t col_run  = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLUE);
    uint8_t col_rdy  = VGA_COLOR(VGA_LIGHT_CYAN,  VGA_BLUE);
    uint8_t col_blk  = VGA_COLOR(VGA_YELLOW,      VGA_BLUE);
    uint8_t col_dead = VGA_COLOR(VGA_LIGHT_GREY,  VGA_BLUE);

    shell_puts("\n  Scheduler Visualizer\n", COL_INFO);
    shell_puts("----------------------------------------------\n", COL_DIM);

    /* time slice bar */
    shell_puts("  Time Slice: [", COL_OUTPUT);
    int filled = (int)(tr * 20 / quantum);
    if (filled < 0)  filled = 0;
    if (filled > 20) filled = 20;
    for (int i = 0; i < 20; i++)
        shell_putc(i < filled ? '=' : '.', i < filled ? COL_SUCCESS : COL_DIM);
    shell_puts("] ", COL_OUTPUT);
    shell_putuint(ms_left, COL_KEY);
    shell_puts("ms / ", COL_DIM);
    shell_putuint(ms_total, COL_KEY);
    shell_puts("ms\n", COL_DIM);

    shell_puts("----------------------------------------------\n", COL_DIM);
    shell_puts("  Process Queue:\n", COL_INFO);
    shell_putc('\n', COL_OUTPUT);

    /* walk the queue */
    extern pcb_t *sched_current(void);
    pcb_t *cur = sched_current();

    /* we need queue — use sched_print logic but visual */
    /* access queue via current->next traversal */
    pcb_t *p     = cur;
    int    count = 0;

    /* find queue head (idle = pid 0) */
    pcb_t *head = cur;
    int    limit = 64;
    while (head->pid != 0 && limit-- > 0) head = head->next;

    p = head;
    do {
        uint8_t col;
        const char *label;
        const char *bar_char;

        switch (p->state) {
            case PROC_RUNNING:
                col = col_run; label = "RUNNING"; bar_char = "█"; break;
            case PROC_READY:
                col = col_rdy; label = "READY  "; bar_char = "░"; break;
            case PROC_BLOCKED:
                col = col_blk; label = "BLOCKED"; bar_char = "▒"; break;
            default:
                col = col_dead; label = "DEAD   "; bar_char = " "; break;
        }

        shell_puts("  [ ", COL_DIM);
        shell_puts(label, col);
        shell_puts(" ] ", COL_DIM);
        shell_puts(p->name, col);

        /* time slice indicator for running process */
        if (p->state == PROC_RUNNING) {
            shell_puts("  ", COL_DIM);
            shell_puts("[", COL_DIM);
            for (int i = 0; i < 20; i++)
                shell_putc(i < filled ? '=' : '.', i < filled ? COL_SUCCESS : COL_DIM);
            shell_puts("]", COL_DIM);
            shell_puts("  ", COL_DIM);
            shell_putuint(ms_left, COL_KEY);
            shell_puts("ms left", COL_DIM);
        } else if (p->state == PROC_READY) {
            shell_puts("  waiting...", COL_DIM);
        }

        shell_putc('\n', COL_OUTPUT);
        (void)bar_char;

        p = p->next;
        count++;
    } while (p != head && count < 64);

    shell_puts("\n----------------------------------------------\n", COL_DIM);
    shell_puts("  Quantum  : ", COL_OUTPUT);
    shell_putuint(quantum, COL_KEY);
    shell_puts(" ticks (", COL_DIM);
    shell_putuint(ms_total, COL_KEY);
    shell_puts("ms)\n", COL_DIM);
    shell_puts("  Hz       : ", COL_OUTPUT);
    shell_putuint((uint64_t)hz, COL_KEY);
    shell_puts(" Hz\n", COL_DIM);
    shell_puts("  Uptime   : ", COL_OUTPUT);
    uint64_t secs = hz ? pit_ticks() / hz : 0;
    shell_putuint(secs, COL_KEY);
    shell_puts("s\n", COL_DIM);
}
static void cmd_virt2phys(int argc, char **argv) {
    if (argc < 2) {
        shell_puts("\n  Usage: virt2phys <address>\n", COL_ERROR);
        shell_puts("  Example: virt2phys 0x401000\n", COL_ERROR);
        return;
    }

    uint64_t virt = (uint64_t)parse_int(argv[1]);
    shell_putc('\n', COL_OUTPUT);

    /* extract page table indices */
    uint64_t pml4_idx = (virt >> 39) & 0x1FF;
    uint64_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint64_t pd_idx   = (virt >> 21) & 0x1FF;
    uint64_t pt_idx   = (virt >> 12) & 0x1FF;
    uint64_t offset   = virt & 0xFFF;

    shell_puts("  Virtual  : ", COL_INFO); shell_puthex(virt, COL_OUTPUT); shell_putc('\n', COL_OUTPUT);
    shell_puts("----------------------------------------------\n", COL_DIM);

    /* show index breakdown */
    shell_puts("  PML4 index : [", COL_OUTPUT);
    shell_putuint(pml4_idx, COL_KEY);
    shell_puts("]\n", COL_OUTPUT);

    shell_puts("  PDPT index : [", COL_OUTPUT);
    shell_putuint(pdpt_idx, COL_KEY);
    shell_puts("]\n", COL_OUTPUT);

    shell_puts("  PD   index : [", COL_OUTPUT);
    shell_putuint(pd_idx, COL_KEY);
    shell_puts("]\n", COL_OUTPUT);

    shell_puts("  PT   index : [", COL_OUTPUT);
    shell_putuint(pt_idx, COL_KEY);
    shell_puts("]\n", COL_OUTPUT);

    shell_puts("  Offset     : ", COL_OUTPUT);
    shell_puthex(offset, COL_KEY);
    shell_putc('\n', COL_OUTPUT);
    shell_puts("----------------------------------------------\n", COL_DIM);

    /* walk the page tables */
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    uint64_t *pml4 = (uint64_t *)(cr3 & 0xFFFFFFFFFFFFF000ULL);

    uint64_t pml4e = pml4[pml4_idx];
    shell_puts("  PML4[", COL_OUTPUT); shell_putuint(pml4_idx, COL_KEY);
    shell_puts("] = ", COL_OUTPUT); shell_puthex(pml4e, COL_DIM);
    if (!(pml4e & 1)) {
        shell_puts("  NOT PRESENT\n", COL_ERROR);
        shell_puts("  Translation FAILED\n", COL_ERROR);
        return;
    }
    shell_puts("  PRESENT\n", COL_SUCCESS);

    uint64_t *pdpt = (uint64_t *)(pml4e & 0x000FFFFFFFFFF000ULL);
    uint64_t pdpte = pdpt[pdpt_idx];
    shell_puts("  PDPT[", COL_OUTPUT); shell_putuint(pdpt_idx, COL_KEY);
    shell_puts("] = ", COL_OUTPUT); shell_puthex(pdpte, COL_DIM);
    if (!(pdpte & 1)) {
        shell_puts("  NOT PRESENT\n", COL_ERROR);
        shell_puts("  Translation FAILED\n", COL_ERROR);
        return;
    }
    shell_puts("  PRESENT\n", COL_SUCCESS);

    uint64_t *pd = (uint64_t *)(pdpte & 0x000FFFFFFFFFF000ULL);
    uint64_t pde = pd[pd_idx];
    shell_puts("  PD  [", COL_OUTPUT); shell_putuint(pd_idx, COL_KEY);
    shell_puts("] = ", COL_OUTPUT); shell_puthex(pde, COL_DIM);
    if (!(pde & 1)) {
        shell_puts("  NOT PRESENT\n", COL_ERROR);
        shell_puts("  Translation FAILED\n", COL_ERROR);
        return;
    }

    /* check for 2MB huge page */
    if (pde & (1ULL << 7)) {
        shell_puts("  HUGE (2MB)\n", COL_KEY);
        uint64_t phys = (pde & 0x000FFFFFFFE00000ULL) + (virt & 0x1FFFFF);
        shell_puts("----------------------------------------------\n", COL_DIM);
        shell_puts("  Frame    : ", COL_INFO); shell_puthex(pde & 0x000FFFFFFFE00000ULL, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
        shell_puts("  Physical : ", COL_INFO); shell_puthex(phys, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
        shell_puts("  (2MB huge page - no PT level)\n", COL_DIM);
        return;
    }
    shell_puts("  PRESENT\n", COL_SUCCESS);

    uint64_t *pt = (uint64_t *)(pde & 0x000FFFFFFFFFF000ULL);
    uint64_t pte = pt[pt_idx];
    shell_puts("  PT  [", COL_OUTPUT); shell_putuint(pt_idx, COL_KEY);
    shell_puts("] = ", COL_OUTPUT); shell_puthex(pte, COL_DIM);
    if (!(pte & 1)) {
        shell_puts("  NOT PRESENT\n", COL_ERROR);
        shell_puts("  Translation FAILED\n", COL_ERROR);
        return;
    }
    shell_puts("  PRESENT\n", COL_SUCCESS);

    uint64_t frame = pte & 0x000FFFFFFFFFF000ULL;
    uint64_t phys  = frame + offset;

    shell_puts("----------------------------------------------\n", COL_DIM);
    shell_puts("  Frame    : ", COL_INFO); shell_puthex(frame, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  Offset   : ", COL_INFO); shell_puthex(offset, COL_KEY); shell_putc('\n', COL_OUTPUT);
    shell_puts("  Physical : ", COL_INFO); shell_puthex(phys, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);

    /* flags */
    shell_puts("  Flags    : ", COL_INFO);
    if (pte & (1ULL << 1)) shell_puts("RW ", COL_KEY); else shell_puts("RO ", COL_DIM);
    if (pte & (1ULL << 2)) shell_puts("USER ", COL_KEY); else shell_puts("KERN ", COL_DIM);
    if (pte & (1ULL << 63)) shell_puts("NX", COL_ERROR); else shell_puts("X", COL_SUCCESS);
    shell_putc('\n', COL_OUTPUT);
}

static void cmd_regs(int argc, char **argv) {
    (void)argc; (void)argv;

    uint64_t rax, rbx, rcx, rdx, rsi, rdi;
    uint64_t rsp, rbp, r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t cr0, cr2, cr3, cr4;
    uint64_t rflags, rip;

    __asm__ volatile(
        "mov %%rax, %0\n" "mov %%rbx, %1\n"
        "mov %%rcx, %2\n" "mov %%rdx, %3\n"
        "mov %%rsi, %4\n" "mov %%rdi, %5\n"
        : "=m"(rax), "=m"(rbx), "=m"(rcx),
          "=m"(rdx), "=m"(rsi), "=m"(rdi)
    );
    __asm__ volatile(
        "mov %%rsp, %0\n" "mov %%rbp, %1\n"
        "mov %%r8,  %2\n" "mov %%r9,  %3\n"
        "mov %%r10, %4\n" "mov %%r11, %5\n"
        : "=m"(rsp), "=m"(rbp), "=m"(r8),
          "=m"(r9),  "=m"(r10), "=m"(r11)
    );
    __asm__ volatile(
        "mov %%r12, %0\n" "mov %%r13, %1\n"
        "mov %%r14, %2\n" "mov %%r15, %3\n"
        : "=m"(r12), "=m"(r13), "=m"(r14), "=m"(r15)
    );
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));
    __asm__ volatile("leaq 1f(%%rip), %0\n1:" : "=r"(rip));

    shell_puts("\n  CPU Registers:\n", COL_INFO);
    shell_puts("----------------------------------------------\n", COL_DIM);

    shell_puts("  RAX : ", COL_OUTPUT); shell_puthex(rax, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RBX : ", COL_OUTPUT); shell_puthex(rbx, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RCX : ", COL_OUTPUT); shell_puthex(rcx, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RDX : ", COL_OUTPUT); shell_puthex(rdx, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RSI : ", COL_OUTPUT); shell_puthex(rsi, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  RDI : ", COL_OUTPUT); shell_puthex(rdi, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("----------------------------------------------\n", COL_DIM);
    shell_puts("  RSP : ", COL_OUTPUT); shell_puthex(rsp, COL_KEY);     shell_putc('\n', COL_OUTPUT);
    shell_puts("  RBP : ", COL_OUTPUT); shell_puthex(rbp, COL_KEY);     shell_putc('\n', COL_OUTPUT);
    shell_puts("  RIP : ", COL_OUTPUT); shell_puthex(rip, COL_KEY);     shell_puts("  (approx)\n", COL_DIM);
    shell_puts("----------------------------------------------\n", COL_DIM);
    shell_puts("  R8  : ", COL_OUTPUT); shell_puthex(r8,  COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R9  : ", COL_OUTPUT); shell_puthex(r9,  COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R10 : ", COL_OUTPUT); shell_puthex(r10, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R11 : ", COL_OUTPUT); shell_puthex(r11, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R12 : ", COL_OUTPUT); shell_puthex(r12, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R13 : ", COL_OUTPUT); shell_puthex(r13, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R14 : ", COL_OUTPUT); shell_puthex(r14, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("  R15 : ", COL_OUTPUT); shell_puthex(r15, COL_SUCCESS); shell_putc('\n', COL_OUTPUT);
    shell_puts("----------------------------------------------\n", COL_DIM);
    shell_puts("  CR0 : ", COL_OUTPUT); shell_puthex(cr0, COL_KEY);     shell_putc('\n', COL_OUTPUT);
    shell_puts("  CR2 : ", COL_OUTPUT); shell_puthex(cr2, COL_KEY);     shell_puts("  (page fault addr)\n", COL_DIM);
    shell_puts("  CR3 : ", COL_OUTPUT); shell_puthex(cr3, COL_KEY);     shell_puts("  (PML4 phys)\n", COL_DIM);
    shell_puts("  CR4 : ", COL_OUTPUT); shell_puthex(cr4, COL_KEY);     shell_putc('\n', COL_OUTPUT);
    shell_puts("----------------------------------------------\n", COL_DIM);
    shell_puts("  RFLAGS: ", COL_OUTPUT); shell_puthex(rflags, COL_KEY);
    shell_puts("  [", COL_DIM);
    if (rflags & (1 << 0))  shell_puts("CF ", COL_SUCCESS);
    if (rflags & (1 << 6))  shell_puts("ZF ", COL_SUCCESS);
    if (rflags & (1 << 7))  shell_puts("SF ", COL_SUCCESS);
    if (rflags & (1 << 9))  shell_puts("IF ", COL_SUCCESS);
    if (rflags & (1 << 10)) shell_puts("DF ", COL_SUCCESS);
    if (rflags & (1 << 11)) shell_puts("OF ", COL_SUCCESS);
    shell_puts("]\n", COL_DIM);
}
/* ── updated switchlog ───────────────────────────────────────────────────── */
static void cmd_switchlog(int argc, char **argv) {
    (void)argc; (void)argv;

    if (sched_switchlog_active()) {
        sched_switchlog_disable();
        int n = sched_switchlog_count();

        shell_puts("\n  Switch Log\n", COL_INFO);
        shell_puts("  ---------------------------------------------\n", COL_DIM);
        shell_puts("  idle<->shell noise filtered\n", COL_DIM);
        shell_puts("  ---------------------------------------------\n", COL_DIM);

        if (!n) {
            shell_puts("  No interesting switches recorded.\n", COL_DIM);
            shell_puts("  Tip: enable switchlog, run stress,\n", COL_DIM);
            shell_puts("  then call switchlog again.\n", COL_DIM);
            sched_switchlog_clear();
            return;
        }

        shell_puts("  #seq  +tick   From       ->  To\n", COL_INFO);
        shell_puts(" ----------------------------------------------\n", COL_DIM);

        int start = n > 32 ? n - 32 : 0;

        for (int i = start; i < n; i++) {
            switch_entry_t *e = sched_switchlog_get(i);
            if (!e) continue;

            shell_puts("  #", COL_DIM);
            shell_putuint((uint64_t)e->seq, COL_DIM);
            shell_puts("  +", COL_DIM);
            shell_putuint(e->tick, COL_DIM);
            shell_puts("t  ", COL_DIM);
            shell_puts(e->from, COL_KEY);
            shell_puts(" -> ", COL_DIM);
            shell_puts(e->to, COL_SUCCESS);
            shell_putc('\n', COL_OUTPUT); 
        }

        if (n > 32) {
            shell_puts("  ... (last 32 of ", COL_DIM);
            shell_putuint((uint64_t)n, COL_DIM);
            shell_puts(" total)\n", COL_DIM);
        }

        shell_puts("  ---------------------------------------------\n", COL_DIM);
        shell_puts("  Interesting switches : ", COL_INFO);
        shell_putuint((uint64_t)n, COL_OUTPUT);
        shell_putc('\n', COL_OUTPUT);
        sched_switchlog_clear();

    } else {
        sched_switchlog_clear();
        sched_switchlog_enable();
        shell_puts("\n  Switch logging ON\n", COL_SUCCESS);
        shell_puts("  Ticks shown are relative to log start.\n", COL_DIM);
        shell_puts("  Run stress then switchlog again.\n", COL_DIM);
    }
}

int shell_register(const char *name, const char *desc, shell_cmd_fn fn) {
    if (cmd_count >= SHELL_MAX_CMDS) return -1;
    cmd_table[cmd_count].name = name;
    cmd_table[cmd_count].desc = desc;
    cmd_table[cmd_count].fn   = fn;
    cmd_count++;
    return 0;
}

void shell_init(void) {
    cmd_count = 0; input_len = 0; input_buf[0] = '\0';
    history_count = 0; history_head = 0; history_pos = -1; shift_held = 0;

    shell_register("help",    "List all commands",              cmd_help);
    shell_register("clear",   "Clear the screen",              cmd_clear);
    shell_register("uptime",  "Show uptime and tick count",    cmd_uptime);
    shell_register("mem",     "Memory stats and usage bar",    cmd_mem);
    shell_register("ps",      "List processes and states",     cmd_ps);
    shell_register("lsmem",   "E820 memory map table",         cmd_lsmem);
    shell_register("memtest", "Alloc/write/verify/free pages", cmd_memtest);
    shell_register("stress",  "Scheduler stress workers",      cmd_stress);
    shell_register("calc",    "calc <a> <op> <b>",             cmd_calc);
    shell_register("vmmap",   "Virtual memory layout",         cmd_vmmap);
    shell_register("cpuinfo", "CPU info via CPUID",            cmd_cpuinfo);
    shell_register("hexdump", "hexdump <addr> [lines]",        cmd_hexdump);
    shell_register("color",   "VGA 16-color palette",          cmd_color);
    shell_register("history", "Command history buffer",        cmd_history);
    shell_register("version", "Kernel version and build info", cmd_version);
    shell_register("echo",    "Print arguments",               cmd_echo);
    shell_register("reboot",  "Reboot the system",             cmd_reboot);
    shell_register("halt",    "Halt the CPU",                  cmd_halt);
    shell_register("virt2phys","Translate virtual -> physical address", cmd_virt2phys);
    shell_register("switchlog", "Log and display context switches",    cmd_switchlog);
    shell_register("regs",      "Display CPU register state",          cmd_regs);
    shell_register("stack",    "Dump current stack contents",         cmd_stack);
    shell_register("trace",    "trace on|off  Live kernel events",    cmd_trace);
    shell_register("schedviz", "Visual scheduler state + timing",     cmd_schedviz);

    hw_cursor_enable();
    shell_ready = 1;
    serial_puts("[shell] Initialised with ");
    serial_putdec((uint64_t)cmd_count);
    serial_puts(" commands\n");
}

static void shell_execute(char *line) {
    if (!kstrlen(line)) return;
    history_push(line);
    char *argv[SHELL_MAX_ARGS]; int argc = tokenise(line, argv);
    if (!argc) return;
    for (int i = 0; i < cmd_count; i++)
        if (kstrcmp(argv[0], cmd_table[i].name) == 0) {
            cmd_table[i].fn(argc, argv); return;
        }
    shell_puts("\n  Unknown: '", COL_ERROR);
    shell_puts(argv[0], COL_ERROR);
    shell_puts("'  (try 'help')\n", COL_ERROR);
}

void shell_set_shift(int held) { shift_held = held; }

void shell_putchar(char c) {
    if (!shell_ready) return;
    if (c == '\t') { shell_tab_complete(); return; }
    if (c == '\n' || c == '\r') {
        input_buf[input_len] = '\0';
        char line[SHELL_MAX_INPUT]; kstrcpy(line, input_buf);
        input_len = 0; input_buf[0] = '\0'; history_pos = -1;
        shell_execute(line); print_prompt(); return;
    }
    if (c == '\b') {
        if (input_len > 0) {
            input_len--; input_buf[input_len] = '\0';
            vga_putc('\b', COL_OUTPUT);
            serial_putc('\b'); serial_putc(' '); serial_putc('\b');
            update_hw_cursor();
        }
        return;
    }
    if (input_len < SHELL_MAX_INPUT - 1) {
        input_buf[input_len++] = c; input_buf[input_len] = '\0';
        vga_putc(c, COL_OUTPUT); serial_putc(c); update_hw_cursor();
    }
}

void shell_special(uint8_t sc) {
    if (!shell_ready) return;
    if (sc == 0x48) {
        int n = (history_pos < 0) ? 1 : history_pos + 1;
        const char *e = history_get(n);
        if (!e) return;
        history_pos = n;
        int len = kstrlen(e);
        for (int i = 0; i < len && i < SHELL_MAX_INPUT - 1; i++) input_buf[i] = e[i];
        input_buf[len] = '\0'; input_len = len; redraw_input(); return;
    }
    if (sc == 0x50) {
        if (history_pos <= 0) {
            history_pos = -1; input_len = 0; input_buf[0] = '\0'; redraw_input(); return;
        }
        history_pos--;
        if (history_pos == 0) {
            history_pos = -1; input_len = 0; input_buf[0] = '\0';
        } else {
            const char *e = history_get(history_pos);
            if (e) {
                int len = kstrlen(e);
                for (int i = 0; i < len && i < SHELL_MAX_INPUT - 1; i++) input_buf[i] = e[i];
                input_buf[len] = '\0'; input_len = len;
            }
        }
        redraw_input(); return;
    }
}

void shell_run(void) {
    print_prompt();
    while (1) sched_yield();
}