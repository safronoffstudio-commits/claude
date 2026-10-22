@echo off
rem SamOS в QEMU для Windows. Нужен QEMU: https://qemu.weilnetz.de/w64/
rem Мышь в окне работает как палец на сенсорном экране, Esc — «домой», F1 — пункт управления.
cd /d "%~dp0"
set KERNEL=dist\samos.elf
if not exist %KERNEL% set KERNEL=build\samos.elf
qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a72 -m 512M ^
  -global virtio-mmio.force-legacy=false -device ramfb ^
  -device virtio-tablet-device -device virtio-keyboard-device ^
  -rtc base=utc -serial stdio -kernel %KERNEL%
