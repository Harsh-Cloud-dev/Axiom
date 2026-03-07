#include "serial.h"

/* UART register offsets */
#define UART_DATA      0
#define UART_IER       1
#define UART_BAUD_LOW  0
#define UART_BAUD_HIGH 1
#define UART_FCR       2
#define UART_LCR       3
#define UART_MCR       4
#define UART_LSR       5

#define LSR_THR_EMPTY  0x20
#define LCR_8N1        0x03
#define LCR_DLAB       0x80
#define MCR_DTR        0x01
#define MCR_RTS        0x02
#define MCR_OUT2       0x08
#define MCR_LOOPBACK   0x10
#define FCR_ENABLE     0x01
#define FCR_CLEAR_RX   0x02
#define FCR_CLEAR_TX   0x04
#define FCR_TRIG_14    0xC0
#define BAUD_115200    1

static uint16_t active_port = 0;

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

int serial_init(uint16_t port) {
    outb(port + UART_IER,  0x00);
    outb(port + UART_LCR,  LCR_DLAB);
    outb(port + UART_BAUD_LOW,  (uint8_t)(BAUD_115200 & 0xFF));
    outb(port + UART_BAUD_HIGH, (uint8_t)((BAUD_115200 >> 8) & 0xFF));
    outb(port + UART_LCR,  LCR_8N1);
    outb(port + UART_FCR,  FCR_ENABLE | FCR_CLEAR_RX | FCR_CLEAR_TX | FCR_TRIG_14);
    outb(port + UART_MCR,  MCR_DTR | MCR_RTS | MCR_OUT2);

    /* Loopback test */
    outb(port + UART_MCR, MCR_LOOPBACK);
    outb(port + UART_DATA, 0xAE);
    if (inb(port + UART_DATA) != 0xAE) return -1;

    outb(port + UART_MCR, MCR_DTR | MCR_RTS | MCR_OUT2);
    active_port = port;
    return 0;
}

static inline void wait_tx(void) {
    while (!(inb(active_port + UART_LSR) & LSR_THR_EMPTY));
}

void serial_putc(char c) {
    if (!active_port) return;
    if (c == '\n') { wait_tx(); outb(active_port + UART_DATA, '\r'); }
    wait_tx();
    outb(active_port + UART_DATA, (uint8_t)c);
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}

void serial_puthex(uint64_t val) {
    const char *d = "0123456789abcdef";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4)
        serial_putc(d[(val >> i) & 0xF]);
}

void serial_putdec(uint64_t val) {
    if (val == 0) { serial_putc('0'); return; }
    char buf[20];
    int  i = 0;
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    for (int j = i - 1; j >= 0; j--) serial_putc(buf[j]);
}

static void putdec_signed(int64_t val) {
    if (val < 0) { serial_putc('-'); serial_putdec((uint64_t)(-val)); }
    else serial_putdec((uint64_t)val);
}

static void puthex_raw(uint64_t val, int upper) {
    const char *lo = "0123456789abcdef";
    const char *hi = "0123456789ABCDEF";
    const char *d  = upper ? hi : lo;
    int started = 0;
    for (int i = 60; i >= 0; i -= 4) {
        int n = (val >> i) & 0xF;
        if (n || started || i == 0) { serial_putc(d[n]); started = 1; }
    }
}

/* Minimal va_list using GCC builtins — no stdarg.h needed */
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_arg(v,l)   __builtin_va_arg(v,l)
#define va_end(v)     __builtin_va_end(v)

void serial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    while (*fmt) {
        if (*fmt != '%') { serial_putc(*fmt++); continue; }
        fmt++;
        switch (*fmt) {
            case 's': { const char *s = va_arg(args, const char *);
                        serial_puts(s ? s : "(null)"); break; }
            case 'c': serial_putc((char)va_arg(args, int)); break;
            case 'd': putdec_signed((int64_t)va_arg(args, long long)); break;
            case 'u': serial_putdec((uint64_t)va_arg(args, unsigned long long)); break;
            case 'x': puthex_raw((uint64_t)va_arg(args, unsigned long long), 0); break;
            case 'X': puthex_raw((uint64_t)va_arg(args, unsigned long long), 1); break;
            case 'p': {
                serial_puts("0x");
                uint64_t p = (uint64_t)va_arg(args, void *);
                for (int i = 60; i >= 0; i -= 4)
                    serial_putc("0123456789abcdef"[(p >> i) & 0xF]);
                break;
            }
            case '%': serial_putc('%'); break;
            default:  serial_putc('%'); serial_putc(*fmt); break;
        }
        fmt++;
    }
    va_end(args);
}