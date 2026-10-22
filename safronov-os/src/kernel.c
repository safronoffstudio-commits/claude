/* Safronov OS kernel entry point. */
#include "gui.h"

static char brand[49], vendor[13];

static void cpu_detect(void)
{
    uint32_t r[4];
    cpuid(0, 0, &r[0], &r[1], &r[2], &r[3]);
    memcpy(vendor, &r[1], 4);
    memcpy(vendor + 4, &r[3], 4);
    memcpy(vendor + 8, &r[2], 4);
    cpuid(0x80000000, 0, &r[0], &r[1], &r[2], &r[3]);
    if (r[0] >= 0x80000004) {
        for (uint32_t i = 0; i < 3; i++) {
            cpuid(0x80000002 + i, 0, &r[0], &r[1], &r[2], &r[3]);
            memcpy(brand + i * 16, r, 16);
        }
    }
    char *p = brand;
    while (*p == ' ')
        p++;
    memmove(brand, p, strlen(p) + 1);
    if (!brand[0])
        str_copy(brand, vendor, sizeof brand);
}

const char *cpu_brand(void) { return brand; }
const char *cpu_vendor(void) { return vendor; }

static void ok(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    con_color(theme->dim);
    con_write("[");
    con_color(theme->ok);
    con_write(" OK ");
    con_color(theme->dim);
    con_write("] ");
    con_reset_color();
    con_write(buf);
    con_write("\n");
    gfx_flush();
}

bool desktop_mode;

static int boot_step;

static void progress(void)
{
    if (desktop_mode)
        gui_boot_progress(++boot_step * 100 / 9, NULL);
}

void kmain(uint32_t magic, uint64_t info)
{
    bool mb_ok = mb2_parse(magic, info);
    mm_init();
    idt_init();
    gfx_init();
    timer_init();
    con_init();
    desktop_mode = scr.graphics && !cmdline_has("text") && gui_init();
    if (desktop_mode)
        con_set_sink(&con_log_sink);       /* the boot log goes to dmesg */
    progress();
    cpu_detect();
    rand_seed(rdtsc());

    if (!mb_ok)
        kprintf("Внимание: загрузчик не Multiboot2, работаю как получится.\n");
    ok("Загрузчик: %s (%s)", boot.loader[0] ? boot.loader : "?", boot.efi ? "UEFI" : "BIOS");
    if (scr.graphics)
        ok("Экран: %ux%u, %u бит", scr.width, scr.height, boot.fb_bpp);
    else
        ok("Экран: текстовый режим %dx%d", scr.cols, scr.rows);
    ok("Процессор: %s", cpu_brand());
    ok("Память: %lu МБ, свободно под кучу %lu МБ", mm_total_ram() >> 20,
       (mm_heap_size() - mm_heap_used()) >> 20);
    ok("Прерывания и таймер: %s", timer_irq_works() ? "IRQ 1000 Гц" : "режим опроса (IRQ не пришли)");
    progress();
    bool ps2 = kbd_init();
    ok("Клавиатура: %s, раскладки EN и RU", ps2 ? "PS/2-контроллер на месте" : "PS/2-контроллера нет");
    progress();
    bool ps2_mouse = mouse_init();
    ok("Мышь PS/2: %s", ps2_mouse ? (mouse_has_wheel() ? "есть, с колесом" : "есть") : "нет");
    progress();
    /* Under BIOS the firmware usually emulates PS/2 for USB keyboards; we only
     * take controllers it does not hold. On UEFI we take them all.
     * "usb" forces the driver, "nousb" disables it. */
    if (!cmdline_has("nousb")) {
        int kb = usb_init(boot.efi || cmdline_has("usb"));
        if (usb_firmware_controllers())
            ok("USB: xHCI у системы: %d, у прошивки (эмуляция PS/2): %d, клавиатур: %d, мышей: %d",
               usb_controllers(), usb_firmware_controllers(), kb, usb_mice());
        else
            ok("USB: контроллеров xHCI: %d, клавиатур: %d, мышей: %d", usb_controllers(), kb, usb_mice());
    } else {
        ok("USB: свой драйвер выключен (nousb)");
    }
    progress();
    acpi_init();
    ok("ACPI: %s", acpi_present() ? "найден, выключение доступно" : "нет, выключай кнопкой");
    progress();
    fs_init();
    ok("Файловая система: %d файлов в оперативной памяти", fs_count());
    progress();

    if (desktop_mode) {
        gui_set_wallpaper(0);
        progress();
        sleep_ms(200);
        progress();
        gui_main();
    }
    kprintf("\n");
    shell_run();
}
