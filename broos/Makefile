# BroOS build. Needs: gcc, nasm, ld, grub-mkrescue (grub-pc-bin + grub-efi-amd64-bin), xorriso, mtools.

CC      := gcc
AS      := nasm
LD      := ld

CFLAGS  := -std=gnu11 -O2 -g0 -Wall -Wextra -Werror=implicit-function-declaration \
           -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
           -fno-asynchronous-unwind-tables -fno-tree-loop-distribute-patterns -fwrapv \
           -fno-math-errno -mno-red-zone -m64 -march=x86-64 -msse2
LDFLAGS := -n -nostdlib -static -z max-page-size=0x1000 -z noexecstack --no-warn-rwx-segments -T src/linker.ld

BUILD   := build
SRCS_C  := $(wildcard src/*.c)
SRCS_S  := $(wildcard src/*.asm)
OBJS    := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS_C)) $(patsubst src/%.asm,$(BUILD)/%.asm.o,$(SRCS_S))

KERNEL  := $(BUILD)/broos.elf
ISO     := $(BUILD)/broos.iso

OVMF    ?= /usr/share/ovmf/OVMF.fd
QEMU    ?= qemu-system-x86_64
QFLAGS  ?= -m 512M -serial stdio

.PHONY: all iso run run-uefi run-usb usb clean font assets

all: iso

$(BUILD)/%.o: src/%.c src/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: src/%.asm | $(BUILD)
	$(AS) -f elf64 $< -o $@

$(BUILD)/assets.asm.o: assets/assets.bin

# Regenerate the desktop assets (fonts, icons, cursor, wallpapers).
assets:
	python3 tools/mkassets.py

$(KERNEL): $(OBJS) src/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	grub-file --is-x86-multiboot2 $@

iso: $(ISO)

$(ISO): $(KERNEL) boot/grub.cfg
	rm -rf $(BUILD)/iso
	mkdir -p $(BUILD)/iso/boot/grub
	cp $(KERNEL) $(BUILD)/iso/boot/broos.elf
	cp boot/grub.cfg $(BUILD)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/iso -- -volid BROOS 2>&1 | grep -v '^xorriso : UPDATE' || true
	@test -s $@ && echo "Готово: $@ ($$(du -h $@ | cut -f1))"

# Regenerate src/font.h from the Terminus PSF fonts in fonts/.
font:
	python3 tools/mkfont.py fonts/Uni2-Terminus16.psf.gz fonts/Uni2-TerminusBold32x16.psf.gz > src/font.h

run: $(ISO)
	$(QEMU) $(QFLAGS) -cdrom $(ISO)

run-uefi: $(ISO)
	$(QEMU) $(QFLAGS) -bios $(OVMF) -cdrom $(ISO)

# Boot the image the way a PC boots a flash drive: as a USB mass storage device.
run-usb: $(ISO)
	$(QEMU) $(QFLAGS) -drive if=none,id=stick,format=raw,file=$(ISO) \
	    -device qemu-xhci -device usb-storage,drive=stick

# Write to a real flash drive (Linux): make usb DEV=/dev/sdX   -- ERASES the drive!
usb: $(ISO)
	@test -n "$(DEV)" || (echo "Укажи флешку: make usb DEV=/dev/sdX (всё на ней сотрётся)"; exit 1)
	@lsblk -d -o NAME,SIZE,MODEL,TRAN $(DEV)
	@printf "Стереть $(DEV) и записать BroOS? [y/N] "; read a; [ "$$a" = y ]
	sudo dd if=$(ISO) of=$(DEV) bs=4M status=progress conv=fsync
	sync
	@echo "Флешка готова."

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
