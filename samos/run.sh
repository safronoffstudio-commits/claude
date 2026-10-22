#!/bin/sh
# Запуск SamOS в эмуляторе QEMU (aarch64, машина virt).
# Мышь в окне QEMU работает как палец на сенсорном экране.
cd "$(dirname "$0")"
KERNEL=${KERNEL:-build/samos.elf}
[ -f "$KERNEL" ] || KERNEL=dist/samos.elf
exec qemu-system-aarch64 \
    -M virt,gic-version=2 -cpu cortex-a72 -m 512M \
    -global virtio-mmio.force-legacy=false \
    -device ramfb \
    -device virtio-tablet-device \
    -device virtio-keyboard-device \
    -rtc base=utc \
    -serial stdio \
    -kernel "$KERNEL" "$@"
