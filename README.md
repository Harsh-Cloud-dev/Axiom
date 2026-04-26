# Axiom

> A minimal x86_64 kernel written from scratch — bare metal, no shortcuts.

Axiom is a hobby operating system built to understand what actually happens between pressing the power button and running a program. No UEFI hand-holding, no GRUB abstraction — just assembly, C, and a lot of determination.

---

## Architecture

Axiom boots through a custom 4-stage bootloader entirely written in x86 assembly before handing off to a 64-bit C kernel.

```
Stage 0  →  Stage 1  →  Stage 2  →  Stage 3  →  Kernel
 BIOS        Real         Protected    Long        64-bit C
 MBR         Mode         Mode         Mode        Entry
```

---

## Features

| Subsystem | Description |
|---|---|
| **Bootloader** | Custom 4-stage bootloader; transitions from real → protected → long mode |
| **VGA Driver** | Text-mode VGA output (80×25) |
| **Serial Port** | UART serial output for debugging |
| **Memory Map** | E820 BIOS memory map parsing |
| **PMM** | Physical Memory Manager using a bitmap allocator |
| **VMM** | Virtual Memory Manager with paging |
| **Heap** | Dynamic kernel heap allocator |
| **IDT / ISR** | Interrupt Descriptor Table + Interrupt Service Routines |
| **PIT** | Programmable Interval Timer for system ticks |
| **Scheduler** | Basic round-robin task scheduler |
| **Shell** | Minimal interactive kernel shell |

---

## Project Structure

```
axiom/
├── boot/
│   ├── stage0/     # MBR bootloader (real mode)
│   ├── stage1/     # Protected mode entry
│   ├── stage2/     # Protected mode setup
│   └── stage3/     # Long mode transition
├── kernel/
│   ├── kernel.c    # Kernel main entry
│   ├── vga.c/h     # VGA text driver
│   ├── serial.c/h  # Serial (UART) driver
│   ├── E820.c/h    # BIOS memory map
│   ├── pmm.c/h     # Physical memory manager
│   ├── vmm.c/h     # Virtual memory manager
│   ├── heap.c/h    # Kernel heap
│   ├── idt.c/h     # Interrupt descriptor table
│   ├── idt.asm     # IDT assembly stubs
│   ├── isr.c       # Interrupt service routines
│   ├── pit.c/h     # PIT timer
│   ├── sched.c/h   # Scheduler
│   ├── sched.asm   # Scheduler context switch (asm)
│   ├── shell.c/h   # Kernel shell
│   ├── types.h     # Base type definitions
│   └── linker.ld   # Kernel linker script
└── Makefile
```

---

## Building

### Requirements

- `nasm` — assembler
- `gcc` (cross-compiler targeting `x86_64-elf`) — C compiler
- `ld` — GNU linker
- `qemu-system-x86_64` — for emulation
- `make`

### Build & Run

```bash
# Build everything
make

# Run in QEMU
make run

# Clean build artifacts
make clean
```

---

## Status

Axiom is a **work in progress**. The core kernel is functional — it boots, manages memory, handles interrupts, schedules tasks, and drops into a shell. There's a lot more to build.

**Planned / In Progress:**
- [ ] Filesystem (FAT32 or custom)
- [ ] Keyboard driver (PS/2)
- [ ] Userspace & syscall interface
- [ ] ELF loader
- [ ] Basic process isolation

---

## Why?

Because "how does an OS work?" is one of those questions that deserves a real answer — not a textbook chapter, but actual code running on actual hardware. Axiom is that answer, built one subsystem at a time.

---

## License

[MIT](LICENSE)