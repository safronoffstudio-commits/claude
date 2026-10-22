// SamOS — разбор дерева устройств (Flattened Device Tree), чтобы узнать объём RAM.
#include "kernel.h"

struct bootinfo g_boot;

static u32 be32(const void *p) { return bswap32(*(const u32 *)p); }
static u64 be64(const void *p) {
    const u8 *b = p;
    return ((u64)be32(b) << 32) | be32(b + 4);
}

void fdt_parse(const void *dtb) {
    g_boot.ram_base = RAM_BASE;
    g_boot.ram_size = 512UL << 20;
    g_boot.cpu_count = 0;
    const u8 *h = dtb;
    if (be32(h) != 0xd00dfeed) { kprintf("[fdt] DTB не найден, RAM = 512 МБ\n"); return; }
    g_boot.dtb_ok = true;
    const u8 *st = h + be32(h + 8);
    const char *strs = (const char *)h + be32(h + 12);
    int depth = 0;
    bool in_mem = false, in_cpu = false;
    const u8 *p = st;
    for (;;) {
        u32 tok = be32(p); p += 4;
        if (tok == 1) {                       // BEGIN_NODE
            const char *name = (const char *)p;
            size_t n = strlen(name);
            p += (n + 4) & ~3UL;
            depth++;
            in_mem = depth == 2 && strncmp(name, "memory", 6) == 0;
            in_cpu = depth == 3 && strncmp(name, "cpu@", 4) == 0;
            if (in_cpu) g_boot.cpu_count++;
        } else if (tok == 2) {                // END_NODE
            depth--;
            in_mem = in_cpu = false;
        } else if (tok == 3) {                // PROP
            u32 len = be32(p), nameoff = be32(p + 4);
            const u8 *data = p + 8;
            const char *pname = strs + nameoff;
            if (in_mem && strcmp(pname, "reg") == 0 && len >= 16) {
                g_boot.ram_base = be64(data);
                g_boot.ram_size = be64(data + 8);
            }
            p += 8 + ((len + 3) & ~3UL);
        } else if (tok == 4) {                // NOP
        } else break;                         // END
    }
    if (!g_boot.cpu_count) g_boot.cpu_count = 1;
    kprintf("[fdt] RAM: %lu МБ @ %p, ядер ЦП: %d\n", g_boot.ram_size >> 20,
            (void *)g_boot.ram_base, g_boot.cpu_count);
}
