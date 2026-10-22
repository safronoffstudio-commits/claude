# SamOS — сборка ядра. Нужны только clang + ld.lld (кросс-компиляция под aarch64).
CC      := clang
LD      := ld.lld
TARGET  := --target=aarch64-none-elf -mcpu=cortex-a72

CFLAGS  := $(TARGET) -ffreestanding -nostdlib -nostdinc -O2 -g \
           -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers \
           -fno-stack-protector -fno-pic -fno-unwind-tables \
           -fno-asynchronous-unwind-tables -fno-math-errno -ffp-contract=fast \
           -Iinclude -Icrypto -Inet -Imtproto $(EXTRA)
ASFLAGS := $(TARGET) -Iinclude -Icrypto -Inet -Imtproto

BUILD   := build
SRCS    := $(wildcard kernel/*.c gfx/*.c ui/*.c apps/*.c net/*.c crypto/*.c mtproto/*.c)
ASMS    := $(wildcard kernel/*.S)
OBJS    := $(patsubst %.c,$(BUILD)/%.o,$(SRCS)) $(patsubst %.S,$(BUILD)/%.o,$(ASMS))

all: $(BUILD)/samos.elf

nettest:
	$(MAKE) clean >/dev/null
	$(MAKE) EXTRA=-DNET_SELFTEST

# Проверка криптографии на известных тест-векторах (на хосте)
check:
	clang -I include -I crypto -DSAMOS_HOST -O2 -o /tmp/samos_ctest \
	  crypto/sha1.c crypto/sha256.c crypto/aes.c crypto/bn.c tools/crypto_test.c
	/tmp/samos_ctest

mtprototest:
	python3 tools/gen_testkey.py
	$(MAKE) clean >/dev/null
	$(MAKE) EXTRA="-DNET_SELFTEST -DMTPROTO_TEST"

# Обработчик прерываний не должен трогать регистры FPU/SIMD.
$(BUILD)/kernel/irq.o: CFLAGS += -mgeneral-regs-only
# memcpy/memset не должны превращаться компилятором в вызовы самих себя.
$(BUILD)/kernel/lib.o: CFLAGS += -fno-builtin

$(BUILD)/%.o: %.c $(wildcard include/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/samos.elf: $(OBJS) linker.ld
	$(LD) -T linker.ld -o $@ $(OBJS)
	@echo "==> $@ ($$(stat -c %s $@) байт)"

# Готовый «прошивочный» файл без отладочной информации
dist: $(BUILD)/samos.elf
	@mkdir -p dist
	llvm-objcopy --strip-all $(BUILD)/samos.elf dist/samos.elf
	@echo "==> dist/samos.elf ($$(stat -c %s dist/samos.elf) байт)"

test: $(BUILD)/samos.elf
	python3 tools/demo_test.py

fonts:
	python3 tools/mkfont.py gfx/fontdata.c

run: all
	./run.sh

clean:
	rm -rf $(BUILD)

.PHONY: all run clean fonts dist test check nettest mtprototest
