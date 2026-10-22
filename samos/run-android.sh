#!/data/data/com.termux/files/usr/bin/sh
# SamOS на Android-телефоне (без root, без разблокировки загрузчика).
#
# Телефон эмулирует «железо» SamOS через QEMU внутри Termux, а экран SamOS
# вы видите в VNC-клиенте. Касания пальцем работают как сенсор SamOS.
#
# Один раз:
#   1. Установите Termux (из F-Droid) и VNC-клиент (AVNC / bVNC / RealVNC Viewer).
#   2. В Termux:  pkg install qemu-system-aarch64-headless
#   3. Скопируйте на телефон папку samos (нужны dist/samos.elf и этот скрипт).
# Запуск:
#   sh run-android.sh
#   затем в VNC-клиенте подключитесь к 127.0.0.1:5901
cd "$(dirname "$0")"
KERNEL=dist/samos.elf
[ -f "$KERNEL" ] || KERNEL=build/samos.elf
echo "SamOS запускается… Откройте VNC-клиент и подключитесь к 127.0.0.1:5901"
exec qemu-system-aarch64 \
    -M virt,gic-version=2 -cpu cortex-a72 -m 256M \
    -global virtio-mmio.force-legacy=false \
    -device ramfb \
    -device virtio-tablet-device \
    -device virtio-keyboard-device \
    -rtc base=utc \
    -vnc 127.0.0.1:1 \
    -serial none -monitor none \
    -kernel "$KERNEL"
