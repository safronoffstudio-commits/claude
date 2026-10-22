/* Just enough ACPI to power off (\_S5) and reboot (FADT reset register). */
#include "kernel.h"

struct sdt {
    char sig[4];
    uint32_t len;
    uint8_t rev, checksum;
    char oem[6], oem_table[8];
    uint32_t oem_rev, creator, creator_rev;
} __attribute__((packed));

static bool present;
static uint16_t pm1a_cnt, pm1b_cnt, slp_typa, slp_typb;
static uint32_t smi_cmd;
static uint8_t acpi_enable_val;
static bool s5_found;
static bool reset_supported;
static uint8_t reset_space, reset_value;
static uint64_t reset_addr;

static const struct sdt *table_at(uint64_t addr)
{
    if (!addr)
        return NULL;
    if (addr + 0x10000 > 0x100000000ULL)
        mm_map(addr, 0x10000, MAP_WB);
    const struct sdt *t = (const struct sdt *)addr;
    if (addr + t->len > 0x100000000ULL)
        mm_map(addr, t->len, MAP_WB);
    return t;
}

static uint32_t rd32(const uint8_t *p) { return *(const uint32_t *)p; }
static uint64_t rd64(const uint8_t *p) { return *(const uint64_t *)p; }

static void parse_s5(const struct sdt *dsdt)
{
    const uint8_t *p = (const uint8_t *)dsdt + sizeof *dsdt;
    const uint8_t *end = (const uint8_t *)dsdt + dsdt->len;
    for (; p + 8 < end; p++) {
        if (memcmp(p, "_S5_", 4))
            continue;
        bool named = p[-1] == 0x08 || (p[-2] == 0x08 && p[-1] == '\\');
        if (!named || p[4] != 0x12)                /* NameOp ... PackageOp */
            continue;
        const uint8_t *q = p + 5;
        q += ((*q & 0xC0) >> 6) + 2;               /* PkgLength + NumElements */
        if (*q == 0x0A)                            /* BytePrefix */
            q++;
        slp_typa = (uint16_t)(*q++) << 10;
        if (*q == 0x0A)
            q++;
        slp_typb = (uint16_t)(*q) << 10;
        s5_found = true;
        return;
    }
}

static void parse_fadt(const struct sdt *f)
{
    const uint8_t *b = (const uint8_t *)f;
    smi_cmd = rd32(b + 48);
    acpi_enable_val = b[52];
    pm1a_cnt = (uint16_t)rd32(b + 64);
    pm1b_cnt = (uint16_t)rd32(b + 68);
    uint64_t dsdt = rd32(b + 40);
    if (f->len >= 148 && rd64(b + 140))
        dsdt = rd64(b + 140);
    if (f->len >= 129) {
        reset_supported = rd32(b + 112) & (1 << 10);
        reset_space = b[116];
        reset_addr = rd64(b + 120);
        reset_value = b[128];
    }
    const struct sdt *d = table_at(dsdt);
    if (d && !memcmp(d->sig, "DSDT", 4))
        parse_s5(d);
}

void acpi_init(void)
{
    if (!boot.rsdp_present || memcmp(boot.rsdp, "RSD PTR ", 8))
        return;
    const uint8_t *r = boot.rsdp;
    bool xsdt = r[15] >= 2 && rd64(r + 24);
    const struct sdt *root = table_at(xsdt ? rd64(r + 24) : rd32(r + 16));
    if (!root)
        return;
    int entry = xsdt ? 8 : 4;
    int n = (root->len - sizeof *root) / entry;
    const uint8_t *e = (const uint8_t *)root + sizeof *root;
    for (int i = 0; i < n; i++, e += entry) {
        const struct sdt *t = table_at(xsdt ? rd64(e) : rd32(e));
        if (t && !memcmp(t->sig, "FACP", 4)) {
            parse_fadt(t);
            present = true;
        }
    }
}

bool acpi_present(void) { return present; }

static bool hypervisor(void)
{
    uint32_t a, b, c, d;
    cpuid(1, 0, &a, &b, &c, &d);
    return c & (1u << 31);
}

void power_off(void)
{
    cli();
    if (present && s5_found && pm1a_cnt) {
        if (!(inw(pm1a_cnt) & 1) && smi_cmd && acpi_enable_val) {
            outb(smi_cmd, acpi_enable_val);        /* switch the chipset to ACPI mode */
            for (int i = 0; i < 3000000 && !(inw(pm1a_cnt) & 1); i++)
                io_wait();
        }
        outw(pm1a_cnt, slp_typa | (1 << 13));
        if (pm1b_cnt)
            outw(pm1b_cnt, slp_typb | (1 << 13));
        for (int i = 0; i < 1000000; i++)
            io_wait();
    }
    if (hypervisor()) {
        outw(0x604, 0x2000);                        /* QEMU */
        outw(0xB004, 0x2000);                       /* Bochs, old QEMU */
        outw(0x4004, 0x3400);                       /* VirtualBox */
    }
    sti();
}

void reboot(void)
{
    cli();
    if (reset_supported && reset_addr) {
        if (reset_space == 1)
            outb((uint16_t)reset_addr, reset_value);
        else if (reset_space == 0 && reset_addr < 0x100000000ULL)
            *(volatile uint8_t *)reset_addr = reset_value;
        for (int i = 0; i < 100000; i++)
            io_wait();
    }
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++)
        ;
    outb(0x64, 0xFE);                               /* 8042 pulse reset line */
    for (int i = 0; i < 100000; i++)
        io_wait();
    outb(0xCF9, 0x02);                              /* chipset reset control */
    io_wait();
    outb(0xCF9, 0x06);
    for (int i = 0; i < 100000; i++)
        io_wait();
    /* Last resort: triple fault. */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) null_idt = { 0, 0 };
    __asm__ volatile("lidt %0; int3" :: "m"(null_idt));
    for (;;)
        hlt();
}
