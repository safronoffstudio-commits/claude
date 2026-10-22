/* Memory: extra identity mappings (for MMIO above 4 GiB, write-combining
 * framebuffer) and a simple first-fit heap. */
#include "kernel.h"

extern uint64_t pml4[512];
extern char _kernel_start[], _kernel_end[];

#define PAGE_2M   0x200000ULL
#define PTE_P     0x001ULL
#define PTE_W     0x002ULL
#define PTE_PWT   0x008ULL
#define PTE_PCD   0x010ULL
#define PTE_PS    0x080ULL
#define ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t table_pool[32][512] __attribute__((aligned(4096)));
static int table_used;
static bool pat_ready;

static uint64_t *next_level(uint64_t *table, int idx)
{
    if (!(table[idx] & PTE_P)) {
        if (table_used == (int)ARRAY_SIZE(table_pool))
            return NULL;
        uint64_t *t = table_pool[table_used++];
        memset(t, 0, 4096);
        table[idx] = (uint64_t)t | PTE_P | PTE_W;
    }
    return (uint64_t *)(table[idx] & ADDR_MASK);
}

/* Identity-map [phys, phys+size) with 2 MiB pages. MAP_WC uses PAT entry 1,
 * which we reprogram from write-through to write-combining; MAP_UC uses
 * PAT entry 3 (uncached, as after reset). */
void mm_map(uint64_t phys, uint64_t size, enum map_type type)
{
    bool write_combining = type == MAP_WC;
    if (write_combining && !pat_ready) {
        uint32_t a, b, c, d;
        cpuid(1, 0, &a, &b, &c, &d);
        if (d & (1 << 16)) {
            uint64_t pat = rdmsr(0x277);
            pat = (pat & ~0xFF00ULL) | (0x01ULL << 8); /* PA1 = WC */
            wrmsr(0x277, pat);
            pat_ready = true;
        }
    }
    uint64_t flags = PTE_P | PTE_W | PTE_PS;
    if (type == MAP_UC)
        flags |= PTE_PWT | PTE_PCD;
    else if (write_combining && pat_ready)
        flags |= PTE_PWT;
    uint64_t start = phys & ~(PAGE_2M - 1);
    uint64_t end = (phys + size + PAGE_2M - 1) & ~(PAGE_2M - 1);
    for (uint64_t a = start; a < end; a += PAGE_2M) {
        uint64_t *pdpt = next_level(pml4, (a >> 39) & 511);
        uint64_t *pd = pdpt ? next_level(pdpt, (a >> 30) & 511) : NULL;
        if (!pd)
            return;
        pd[(a >> 21) & 511] = a | flags;
    }
    if (type != MAP_WB)
        __asm__ volatile("wbinvd" ::: "memory");
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax", "memory");
}

/* ---- heap ---- */

struct block {
    size_t size;            /* payload bytes */
    struct block *next;
    uint32_t free;
    uint32_t magic;
    uint64_t pad;
};
#define BLOCK_MAGIC 0xB10CB10C

static struct block *heap_head;
static uint64_t heap_size, heap_used, total_ram;

void mm_init(void)
{
    uint64_t best_base = 0, best_len = 0;
    uint64_t ks = (uint64_t)_kernel_start, ke = ((uint64_t)_kernel_end + 0xFFF) & ~0xFFFULL;

    for (int i = 0; i < boot.mmap_count; i++) {
        struct mmap_entry *m = &boot.mmap[i];
        if (m->type != 1)
            continue;
        total_ram += m->len;
        uint64_t b = m->base, e = m->base + m->len;
        if (b < 0x100000)
            b = 0x100000;
        if (e > 0x100000000ULL)       /* heap must live in the boot identity map */
            e = 0x100000000ULL;
        if (b >= e)
            continue;
        /* Cut out the kernel image; keep the larger remaining piece. */
        if (b < ke && e > ks) {
            uint64_t lo = ks > b ? ks - b : 0, hi = e > ke ? e - ke : 0;
            if (hi >= lo)
                b = ke;
            else
                e = ks;
        }
        if (e > b && e - b > best_len)
            best_base = b, best_len = e - b;
    }
    if (best_len < 0x100000) {
        /* No usable map (should not happen with GRUB): take 16 MiB after the kernel. */
        best_base = ke;
        best_len = 16 << 20;
    }
    best_base = (best_base + 15) & ~15ULL;
    heap_head = (struct block *)best_base;
    heap_head->size = best_len - sizeof(struct block) - 16;
    heap_head->next = NULL;
    heap_head->free = 1;
    heap_head->magic = BLOCK_MAGIC;
    heap_size = heap_head->size;
}

void *kmalloc(size_t n)
{
    n = (n + 15) & ~(size_t)15;
    if (!n)
        n = 16;
    uint64_t f = irq_save();
    for (struct block *b = heap_head; b; b = b->next) {
        if (!b->free || b->size < n)
            continue;
        if (b->size >= n + sizeof(struct block) + 64) {
            struct block *nb = (struct block *)((uint8_t *)(b + 1) + n);
            nb->size = b->size - n - sizeof(struct block);
            nb->next = b->next;
            nb->free = 1;
            nb->magic = BLOCK_MAGIC;
            b->next = nb;
            b->size = n;
        }
        b->free = 0;
        heap_used += b->size;
        irq_restore(f);
        return b + 1;
    }
    irq_restore(f);
    return NULL;
}

/* Zeroed memory for devices, aligned to `align` (a power of two). Never freed. */
void *dma_alloc(size_t size, size_t align)
{
    uint8_t *p = kzalloc(size + align);
    return p ? (void *)(((uintptr_t)p + align - 1) & ~(uintptr_t)(align - 1)) : NULL;
}

void *kzalloc(size_t n)
{
    void *p = kmalloc(n);
    if (p)
        memset(p, 0, n);
    return p;
}

void kfree(void *p)
{
    if (!p)
        return;
    struct block *b = (struct block *)p - 1;
    if (b->magic != BLOCK_MAGIC || b->free)
        return;
    uint64_t f = irq_save();
    b->free = 1;
    heap_used -= b->size;
    for (struct block *c = heap_head; c && c->next;) {
        if (c->free && c->next->free && (uint8_t *)(c + 1) + c->size == (uint8_t *)c->next) {
            c->size += sizeof(struct block) + c->next->size;
            c->next = c->next->next;
        } else {
            c = c->next;
        }
    }
    irq_restore(f);
}

uint64_t mm_total_ram(void) { return total_ram; }
uint64_t mm_heap_size(void) { return heap_size; }
uint64_t mm_heap_used(void) { return heap_used; }
