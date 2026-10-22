// SamOS — видеодрайвер: кадровый буфер ramfb, настраивается через fw_cfg (DMA).
#include "kernel.h"

#define FW_DATA  (FWCFG_BASE + 0x00)
#define FW_SEL   (FWCFG_BASE + 0x08)
#define FW_DMA   (FWCFG_BASE + 0x10)

#define FW_CFG_ID        0x01
#define FW_CFG_FILE_DIR  0x19
#define DMA_CTL_ERROR    0x01
#define DMA_CTL_SELECT   0x08
#define DMA_CTL_WRITE    0x10

#define DRM_FORMAT_XRGB8888 0x34325258  /* 'XR24' */

u32 *g_fb;
int g_fb_w, g_fb_h;

struct fw_dma { u32 control; u32 length; u64 address; } __attribute__((packed));
struct ramfb_cfg { u64 addr; u32 fourcc; u32 flags; u32 width, height, stride; } __attribute__((packed));

static void fw_select(u16 sel) { *(volatile u16 *)FW_SEL = bswap16(sel); }
static void fw_read(void *buf, u32 len) {
    u8 *p = buf;
    while (len--) *p++ = mmio_r8(FW_DATA);
}

static int fw_find_file(const char *name, u32 *size) {
    u32 count;
    fw_select(FW_CFG_FILE_DIR);
    fw_read(&count, 4);
    count = bswap32(count);
    for (u32 i = 0; i < count; i++) {
        struct { u32 size; u16 select; u16 reserved; char name[56]; } f;
        fw_read(&f, sizeof f);
        if (strcmp(f.name, name) == 0) {
            if (size) *size = bswap32(f.size);
            return bswap16(f.select);
        }
    }
    return -1;
}

static bool fw_dma_write(u16 sel, void *data, u32 len) {
    static volatile struct fw_dma acc __attribute__((aligned(16)));
    acc.control = bswap32(((u32)sel << 16) | DMA_CTL_SELECT | DMA_CTL_WRITE);
    acc.length = bswap32(len);
    acc.address = bswap64((u64)(uintptr_t)data);
    dsb();
    *(volatile u64 *)FW_DMA = bswap64((u64)(uintptr_t)&acc);
    dsb();
    u32 ctl;
    while ((ctl = bswap32(acc.control)) & ~DMA_CTL_ERROR) {}
    return !(ctl & DMA_CTL_ERROR);
}

bool fb_init(int w, int h) {
    char sig[4];
    fw_select(0x0000);
    fw_read(sig, 4);
    if (memcmp(sig, "QEMU", 4) != 0) { kprintf("[fb] fw_cfg не найден\n"); return false; }
    int sel = fw_find_file("etc/ramfb", NULL);
    if (sel < 0) { kprintf("[fb] ramfb не подключён (нужно -device ramfb)\n"); return false; }

    g_fb = kmalloc_aligned((size_t)w * h * 4, 4096);
    memset32(g_fb, 0, (size_t)w * h);
    g_fb_w = w;
    g_fb_h = h;

    static struct ramfb_cfg cfg __attribute__((aligned(16)));
    cfg.addr = bswap64((u64)(uintptr_t)g_fb);
    cfg.fourcc = bswap32(DRM_FORMAT_XRGB8888);
    cfg.flags = 0;
    cfg.width = bswap32(w);
    cfg.height = bswap32(h);
    cfg.stride = bswap32(w * 4);
    if (!fw_dma_write((u16)sel, &cfg, sizeof cfg)) { kprintf("[fb] ошибка DMA fw_cfg\n"); return false; }
    kprintf("[fb] ramfb %dx%d XRGB8888 @ %p\n", w, h, g_fb);
    return true;
}
