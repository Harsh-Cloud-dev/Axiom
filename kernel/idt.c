#include "idt.h"

static idt_entry_t idt[256];
static idtr_t      idtr;

extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void io_wait(void) { outb(0x80, 0); }

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void pic_remap(void) {
    uint8_t m1 = inb(PIC1_DATA);
    uint8_t m2 = inb(PIC2_DATA);
    outb(PIC1_CMD,  0x11); io_wait();
    outb(PIC2_CMD,  0x11); io_wait();
    outb(PIC1_DATA, 0x20); io_wait();
    outb(PIC2_DATA, 0x28); io_wait();
    outb(PIC1_DATA, 0x04); io_wait();
    outb(PIC2_DATA, 0x02); io_wait();
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();
    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

static void idt_set_gate(uint8_t vec, void (*handler)(void), uint8_t attr) {
    uint64_t addr        = (uint64_t)handler;
    idt[vec].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vec].selector    = 0x08;
    idt[vec].ist         = 0;
    idt[vec].type_attr   = attr;
    idt[vec].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vec].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vec].zero        = 0;
}

void idt_init(void) {
    pic_remap();
    #define G 0x8E
    idt_set_gate( 0,isr0, G); idt_set_gate( 1,isr1, G);
    idt_set_gate( 2,isr2, G); idt_set_gate( 3,isr3, G);
    idt_set_gate( 4,isr4, G); idt_set_gate( 5,isr5, G);
    idt_set_gate( 6,isr6, G); idt_set_gate( 7,isr7, G);
    idt_set_gate( 8,isr8, G); idt_set_gate( 9,isr9, G);
    idt_set_gate(10,isr10,G); idt_set_gate(11,isr11,G);
    idt_set_gate(12,isr12,G); idt_set_gate(13,isr13,G);
    idt_set_gate(14,isr14,G); idt_set_gate(15,isr15,G);
    idt_set_gate(16,isr16,G); idt_set_gate(17,isr17,G);
    idt_set_gate(18,isr18,G); idt_set_gate(19,isr19,G);
    idt_set_gate(20,isr20,G); idt_set_gate(21,isr21,G);
    idt_set_gate(22,isr22,G); idt_set_gate(23,isr23,G);
    idt_set_gate(24,isr24,G); idt_set_gate(25,isr25,G);
    idt_set_gate(26,isr26,G); idt_set_gate(27,isr27,G);
    idt_set_gate(28,isr28,G); idt_set_gate(29,isr29,G);
    idt_set_gate(30,isr30,G); idt_set_gate(31,isr31,G);

    idt_set_gate(32,irq0, G); idt_set_gate(33,irq1, G);
    idt_set_gate(34,irq2, G); idt_set_gate(35,irq3, G);
    idt_set_gate(36,irq4, G); idt_set_gate(37,irq5, G);
    idt_set_gate(38,irq6, G); idt_set_gate(39,irq7, G);
    idt_set_gate(40,irq8, G); idt_set_gate(41,irq9, G);
    idt_set_gate(42,irq10,G); idt_set_gate(43,irq11,G);
    idt_set_gate(44,irq12,G); idt_set_gate(45,irq13,G);
    idt_set_gate(46,irq14,G); idt_set_gate(47,irq15,G);

    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)&idt;
    __asm__ volatile("lidt %0" : : "m"(idtr));
    __asm__ volatile("sti");
}