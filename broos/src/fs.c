/* BroFS: a flat file system that lives in RAM (gone after reboot). */
#include "kernel.h"

static struct file files[FS_MAX_FILES];
static uint32_t version = 1;

uint32_t fs_version(void)
{
    return version;
}

bool fs_valid_name(const char *name)
{
    size_t n = strlen(name);
    if (!n || n >= FS_NAME_MAX)
        return false;
    for (const char *p = name; *p; p++)
        if (*p == '/' || *p == '>' || (uint8_t)*p < 32)
            return false;
    return true;
}

struct file *fs_find(const char *name)
{
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && !strcmp(files[i].name, name))
            return &files[i];
    return NULL;
}

struct file *fs_at(int i)
{
    return i >= 0 && i < FS_MAX_FILES && files[i].used ? &files[i] : NULL;
}

bool fs_write(const char *name, const char *data, size_t len, bool append)
{
    if (!fs_valid_name(name))
        return false;
    struct file *f = fs_find(name);
    if (!f) {
        for (int i = 0; i < FS_MAX_FILES && !f; i++)
            if (!files[i].used)
                f = &files[i];
        if (!f)
            return false;
        memset(f, 0, sizeof *f);
        str_copy(f->name, name, sizeof f->name);
    }
    size_t keep = append ? f->size : 0;
    char *buf = kmalloc(keep + len + 1);
    if (!buf)
        return false;
    if (keep)
        memcpy(buf, f->data, keep);
    memcpy(buf + keep, data, len);
    buf[keep + len] = 0;
    kfree(f->data);
    f->data = buf;
    f->size = keep + len;
    f->used = true;
    f->trashed = false;
    version++;
    return true;
}

bool fs_delete(const char *name)
{
    struct file *f = fs_find(name);
    if (!f)
        return false;
    kfree(f->data);
    memset(f, 0, sizeof *f);
    version++;
    return true;
}

bool fs_trash(const char *name, bool trashed)
{
    struct file *f = fs_find(name);
    if (!f)
        return false;
    f->trashed = trashed;
    version++;
    return true;
}

void fs_empty_trash(void)
{
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && files[i].trashed) {
            kfree(files[i].data);
            memset(&files[i], 0, sizeof files[i]);
        }
    version++;
}

bool fs_rename(const char *from, const char *to)
{
    struct file *f = fs_find(from);
    if (!f || !fs_valid_name(to) || fs_find(to))
        return false;
    str_copy(f->name, to, sizeof f->name);
    version++;
    return true;
}

size_t fs_total_bytes(void)
{
    size_t n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used)
            n += files[i].size;
    return n;
}

int fs_count(void)
{
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        n += files[i].used && !files[i].trashed;
    return n;
}

static void put(const char *name, const char *text)
{
    fs_write(name, text, strlen(text), false);
}

void fs_init(void)
{
    put("readme.txt",
        "Добро пожаловать в BroOS, бро!\n"
        "\n"
        "Это маленькая 64-битная операционная система, написанная с нуля.\n"
        "Она грузится с флешки через GRUB (BIOS и UEFI) и работает целиком\n"
        "в оперативной памяти: файлы живут до перезагрузки.\n"
        "\n"
        "С чего начать:\n"
        "  help        - список команд\n"
        "  neofetch    - инфо о системе\n"
        "  edit заметка.txt - текстовый редактор\n"
        "  snake       - змейка\n"
        "  mandel      - фрактал Мандельброта\n"
        "\n"
        "Раскладка: Alt+Shift или Ctrl+Shift.\n");
    put("todo.txt",
        "[x] загрузиться\n"
        "[x] не упасть\n"
        "[ ] захватить мир\n");
    put("bro.txt", "Бро не бросает бро. Даже когда Windows ставит обновления.\n");
}
