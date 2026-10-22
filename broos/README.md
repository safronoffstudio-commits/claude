# BroOS 1.0

Маленькая 64-битная операционная система, написанная с нуля на C и ассемблере.
Грузится с флешки на обычном ПК (x86-64) и в режиме **BIOS (Legacy/CSM)**, и в
режиме **UEFI**. Работает целиком в оперативной памяти и не трогает диски
компьютера: Windows/Linux на жёстком диске останутся как были.

## Что умеет

- Своё ядро: long mode, прерывания, таймер, управление памятью, ACPI
  (выключение и перезагрузка), синий экран с регистрами вместо молчаливого зависания.
- Графическая консоль с кириллицей (шрифт Terminus). На экранах от 1600 px
  шрифт крупный, на 4K — ещё крупнее. Если видеорежим не поднялся, работает
  и в старом текстовом режиме VGA.
- Клавиатура PS/2 и **USB** (свой драйвер xHCI: клавиатуры в портах и за
  USB-хабами, горячее подключение, автоповтор). Раскладки EN/RU, переключение
  **Alt+Shift** или **Ctrl+Shift**.
- Шелл `brosh`: история (↑/↓), Tab-дополнение, редактирование строки.
- BroFS — файлы в RAM: `ls`, `cat`, `edit`, `touch`, `cp`, `mv`, `rm`, `echo текст > файл`.
- Текстовый редактор `edit` (Ctrl+S сохранить, Esc выйти).
- `calc`, `date`, `uptime`, `mem`, `neofetch`, цветовые темы (`theme`).
- Игры и демки: `snake`, `mandel` (фрактал с зумом), `matrix`.

## Чего нет (честно)

Это настоящая ОС, но игрушечная: нет драйверов дисков, сети, звука (кроме
PC-спикера), мыши, окон и браузера, программы от Windows/Linux не запустятся.
Файлы живут до перезагрузки. На диск компьютера она не ставится — грузится с
флешки каждый раз.

## Как записать флешку

Нужен файл `broos.iso` (12 МБ) и флешка от 32 МБ. **Всё на флешке сотрётся.**

**Windows — Rufus** (rufus.ie):
1. Устройство: твоя флешка. Метод загрузки: «Диск или ISO-образ» → `broos.iso`.
2. Нажми «Старт». Rufus спросит про режим записи ISOHybrid — выбери
   **«Записать в режиме DD-образа»**.

**Windows / macOS / Linux — balenaEtcher**: Flash from file → `broos.iso` →
выбрать флешку → Flash.

**Linux**:
```sh
lsblk                                   # найди флешку, например /dev/sdb
sudo dd if=broos.iso of=/dev/sdX bs=4M status=progress conv=fsync
```
**macOS (Terminal)**:
```sh
diskutil list                           # найди флешку, например /dev/disk4
diskutil unmountDisk /dev/diskN
sudo dd if=broos.iso of=/dev/rdiskN bs=4m
```

## Как загрузиться

1. Вставь флешку, перезагрузи ПК и сразу жми клавишу меню загрузки:
   F12 (Dell, Lenovo, Acer, Gigabyte), F11 (MSI, ASRock), F8 (ASUS), Esc или F9 (HP),
   F10 / Esc у некоторых ноутбуков. Выбери флешку (можно с пометкой UEFI или без —
   работают оба варианта).
2. **Secure Boot должен быть выключен** (в настройках BIOS/UEFI → Security / Boot).
   Загрузчик не подписан ключом Microsoft, с включённым Secure Boot флешка не стартанёт.
3. Появится меню GRUB → «BroOS 1.0» (загрузится сам через 5 секунд).

Если что-то не так — меню **Advanced options**:

| Проблема | Что выбрать |
|---|---|
| Картинка растянута или мелкая | `native screen resolution` |
| Чёрный экран после выбора | `safe video 800x600` |
| В UEFI не работает USB-клавиатура | `no own USB driver` (и включи *Legacy USB Support* в BIOS) |
| В Legacy/CSM не работает USB-клавиатура | `force own USB keyboard driver` |

Встроенная клавиатура ноутбука почти всегда подключена как PS/2 и работает сразу.
USB-клавиатуру в режиме BIOS обычно обслуживает сама прошивка (эмуляция PS/2), а
если не обслуживает — BroOS забирает USB-контроллер себе; в режиме UEFI драйвер
BroOS включён всегда.
Компьютеры Mac на Apple Silicon (M1–M4) загрузить x86-систему не могут.

## Команды

```
help        список команд              neofetch    инфо о системе
ls / cat    файлы                       edit <файл> редактор
echo текст > файл                       calc (2+3)*7, 2^32, 0xff
date        дата и время                uptime      сколько работает
mem         память и карта памяти       theme       bro, hacker, ocean, amber, light
snake       змейка                      mandel      фрактал (стрелки, +/-, R, Esc)
matrix      матрица                     panic       тестовый синий экран
reboot      перезагрузка                shutdown    выключение
```
Ctrl+Alt+Del — перезагрузка в любой момент.

## Сборка из исходников (Linux)

```sh
sudo apt install build-essential nasm grub-pc-bin grub-efi-amd64-bin grub-common \
                 xorriso mtools qemu-system-x86 ovmf
make            # -> build/broos.iso
make run        # QEMU, BIOS
make run-uefi   # QEMU, UEFI
make run-usb    # QEMU, загрузка образа как USB-флешки
make usb DEV=/dev/sdX   # записать на флешку (спросит подтверждение)
```

`tools/qemu_shot.py` гоняет систему в QEMU без экрана, нажимает клавиши и делает
скриншоты — им всё и тестировалось (BIOS, UEFI, USB-флешка, USB-клавиатура за хабом
без PS/2, разрешения от 720×400 текстом до 2560×1600).

## Устройство

```
boot/grub.cfg   меню загрузчика
src/boot.asm    Multiboot2-заголовок, страницы на 4 ГБ, переход в 64-бит
src/isr.asm     входы прерываний
src/kernel.c    kmain: порядок инициализации
src/mm.c        куча, отображение MMIO (WC для видео, UC для устройств)
src/gfx.c       фреймбуфер, двойная буферизация, текстовая сетка, VGA-текст
src/console.c   терминал и строка статуса
src/idt.c       IDT, PIC, APIC virtual wire, синий экран
src/timer.c     PIT + TSC, работа без прерываний
src/kbd.c       PS/2, раскладки
src/usb.c       xHCI, HID-клавиатуры, хабы
src/acpi.c      выключение (_S5) и перезагрузка
src/fs.c        файлы в RAM
src/shell.c     шелл и команды
src/edit.c      редактор
src/apps.c      neofetch, snake, mandel, matrix
src/font.h      шрифт (генерируется tools/mkfont.py)
```

Шрифт: Terminus Font © Dimitar Toshkov Zhekov, лицензия SIL Open Font License 1.1.
