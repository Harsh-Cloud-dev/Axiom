ASM    = nasm
CC     = x86_64-elf-gcc
LD     = x86_64-elf-ld
OBJCPY = x86_64-elf-objcopy
QEMU   = qemu-system-x86_64

CFLAGS = -ffreestanding -fno-stack-protector -fno-pic \
         -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
         -nostdlib -nostdinc -O2 -Wall -Wextra -Ikernel

all: os.img

# ── Boot stages ───────────────────────────────────────────
boot/stage0/boot.bin: boot/stage0/boot.asm
	$(ASM) -f bin $< -o $@

boot/stage1/pm_entry.bin: boot/stage1/pm_entry.asm
	$(ASM) -f bin $< -o $@

boot/stage2/pm_setup.bin: boot/stage2/pm_setup.asm
	$(ASM) -f bin $< -o $@

boot/stage3/longmode.bin: boot/stage3/longmode.asm
	$(ASM) -f bin $< -o $@

# ── Kernel objects ────────────────────────────────────────
kernel/entry.o: kernel/entry.asm
	$(ASM) -f elf64 $< -o $@

kernel/idt_asm.o: kernel/idt.asm
	$(ASM) -f elf64 $< -o $@

kernel/vga.o: kernel/vga.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/serial.o: kernel/serial.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/idt.o: kernel/idt.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/isr.o: kernel/isr.c
	$(CC) $(CFLAGS) -c $< -o $@

kernel/kernel.o: kernel/kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

KERNEL_OBJS = kernel/entry.o   \
              kernel/idt_asm.o  \
              kernel/vga.o      \
              kernel/serial.o   \
              kernel/idt.o      \
              kernel/isr.o      \
              kernel/kernel.o

kernel/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) -T kernel/linker.ld -o $@ $(KERNEL_OBJS)

kernel/kernel.bin: kernel/kernel.elf
	$(OBJCPY) -O binary $< $@

# ── Disk image ────────────────────────────────────────────
os.img: boot/stage0/boot.bin boot/stage1/pm_entry.bin \
        boot/stage2/pm_setup.bin boot/stage3/longmode.bin \
        kernel/kernel.bin
	dd if=/dev/zero                of=os.img bs=512 count=2880  2>/dev/null
	dd if=boot/stage0/boot.bin     of=os.img bs=512 seek=0  conv=notrunc 2>/dev/null
	dd if=boot/stage1/pm_entry.bin of=os.img bs=512 seek=1  conv=notrunc 2>/dev/null
	dd if=boot/stage2/pm_setup.bin of=os.img bs=512 seek=4  conv=notrunc 2>/dev/null
	dd if=boot/stage3/longmode.bin of=os.img bs=512 seek=8  conv=notrunc 2>/dev/null
	dd if=kernel/kernel.bin        of=os.img bs=512 seek=12 conv=notrunc 2>/dev/null
	@echo ""
	@echo "Disk image layout:"
	@echo "  seek=0  : Stage0  ($$(wc -c < boot/stage0/boot.bin) bytes)"
	@echo "  seek=1  : Stage1  ($$(wc -c < boot/stage1/pm_entry.bin) bytes)"
	@echo "  seek=4  : Stage2  ($$(wc -c < boot/stage2/pm_setup.bin) bytes)"
	@echo "  seek=8  : Stage3  ($$(wc -c < boot/stage3/longmode.bin) bytes)"
	@echo "  seek=12 : Kernel  ($$(wc -c < kernel/kernel.bin) bytes)"

# ── Run  ──────────────────────────────────────────────────
# -serial stdio  → COM1 output appears in your terminal
run: os.img
	$(QEMU) \
		-drive format=raw,file=os.img,if=ide,index=0,media=disk \
		-serial stdio \
		-no-reboot \
		-no-shutdown

# ── Debug ─────────────────────────────────────────────────
debug: os.img
	$(QEMU) \
		-drive format=raw,file=os.img,if=ide,index=0,media=disk \
		-serial stdio \
		-no-reboot \
		-no-shutdown \
		-d int,cpu_reset \
		-D /tmp/qemu.log

# ── Clean ─────────────────────────────────────────────────
clean:
	rm -f boot/stage0/*.bin boot/stage1/*.bin \
	      boot/stage2/*.bin boot/stage3/*.bin \
	      kernel/*.o kernel/*.elf kernel/*.bin os.img