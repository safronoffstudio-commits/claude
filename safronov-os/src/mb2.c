/* Multiboot2 boot information parsing. Everything we need is copied out,
 * so the loader's memory can be reused afterwards. */
#include "kernel.h"

#define MB2_BOOT_MAGIC 0x36D76289

struct bootinfo boot;

static uint32_t rd32(const uint8_t *p) { return *(const uint32_t *)p; }
static uint64_t rd64(const uint8_t *p) { return *(const uint64_t *)p; }

bool mb2_parse(uint32_t magic, uint64_t info)
{
    if (magic != MB2_BOOT_MAGIC)
        return false;
    const uint8_t *base = (const uint8_t *)info;
    uint32_t total = rd32(base);
    const uint8_t *p = base + 8;
    while (p < base + total) {
        uint32_t type = rd32(p), size = rd32(p + 4);
        if (type == 0)
            break;
        switch (type) {
        case 1: /* command line */
            str_copy(boot.cmdline, (const char *)p + 8, sizeof boot.cmdline);
            break;
        case 2: /* boot loader name */
            str_copy(boot.loader, (const char *)p + 8, sizeof boot.loader);
            break;
        case 6: { /* memory map */
            uint32_t esz = rd32(p + 8);
            for (const uint8_t *e = p + 16; e + esz <= p + size && boot.mmap_count < MMAP_MAX; e += esz) {
                struct mmap_entry *m = &boot.mmap[boot.mmap_count++];
                m->base = rd64(e);
                m->len = rd64(e + 8);
                m->type = rd32(e + 16);
            }
            break;
        }
        case 8: /* framebuffer */
            boot.fb_present = true;
            boot.fb_addr = rd64(p + 8);
            boot.fb_pitch = rd32(p + 16);
            boot.fb_width = rd32(p + 20);
            boot.fb_height = rd32(p + 24);
            boot.fb_bpp = p[28];
            boot.fb_type = p[29];
            if (boot.fb_type == 1) {
                boot.r_pos = p[32];
                boot.r_size = p[33];
                boot.g_pos = p[34];
                boot.g_size = p[35];
                boot.b_pos = p[36];
                boot.b_size = p[37];
            }
            break;
        case 11: /* EFI 32-bit system table */
        case 12: /* EFI 64-bit system table */
            boot.efi = true;
            break;
        case 14: /* ACPI 1.0 RSDP */
            if (!boot.rsdp_present) {
                memcpy(boot.rsdp, p + 8, 20);
                boot.rsdp_present = true;
            }
            break;
        case 15: /* ACPI 2.0+ RSDP (preferred) */
            memcpy(boot.rsdp, p + 8, MIN(size - 8, sizeof boot.rsdp));
            boot.rsdp_present = true;
            break;
        }
        p += (size + 7) & ~7u;
    }
    return true;
}
