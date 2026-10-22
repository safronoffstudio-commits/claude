/* The Safronov OS file system: files in RAM (gone after reboot), each in one
 * of a few fixed folders. A file's name is its path: "Документы/план.txt". */
#include "kernel.h"

const char *const fs_folders[FOLDER_COUNT] = {
    "Рабочий стол", "Документы", "Загрузки", "Изображения", "Музыка", "Видео",
};

static struct file files[FS_MAX_FILES];
static uint32_t version = 1;

uint32_t fs_version(void)
{
    return version;
}

int fs_folder_by_name(const char *name, size_t len)
{
    for (int i = 0; i < FOLDER_COUNT; i++)
        if (strlen(fs_folders[i]) == len && !memcmp(fs_folders[i], name, len))
            return i;
    return -1;
}

int fs_folder_of(const char *path)
{
    const char *slash = strchr(path, '/');
    return slash ? fs_folder_by_name(path, slash - path) : -1;
}

const char *fs_base(const char *path)
{
    const char *slash = strchr(path, '/');
    return slash ? slash + 1 : path;
}

void fs_join(char *out, size_t cap, int folder, const char *base)
{
    snprintf(out, cap, "%s/%s", fs_folders[folder], base);
}

bool fs_valid_name(const char *path)
{
    if (strlen(path) >= FS_NAME_MAX || fs_folder_of(path) < 0)
        return false;
    const char *base = fs_base(path);
    if (!*base || *base == ' ')
        return false;
    for (const char *p = base; *p; p++)
        if (*p == '/' || *p == '>' || (uint8_t)*p < 32)
            return false;
    return true;
}

/* A free name like "Документы/Новый файл 2.txt". */
bool fs_unique(char *out, size_t cap, int folder, const char *stem, const char *ext)
{
    for (int n = 1; n < 1000; n++) {
        char base[96];
        if (n == 1)
            snprintf(base, sizeof base, "%s%s", stem, ext);
        else
            snprintf(base, sizeof base, "%s %d%s", stem, n, ext);
        fs_join(out, cap, folder, base);
        if (!fs_find(out))
            return true;
    }
    return false;
}

struct file *fs_find(const char *path)
{
    for (int i = 0; i < FS_MAX_FILES; i++)
        if (files[i].used && !strcmp(files[i].name, path))
            return &files[i];
    return NULL;
}

struct file *fs_at(int i)
{
    return i >= 0 && i < FS_MAX_FILES && files[i].used ? &files[i] : NULL;
}

bool fs_write(const char *path, const char *data, size_t len, bool append)
{
    if (!fs_valid_name(path))
        return false;
    struct file *f = fs_find(path);
    if (!f) {
        for (int i = 0; i < FS_MAX_FILES && !f; i++)
            if (!files[i].used)
                f = &files[i];
        if (!f)
            return false;
        memset(f, 0, sizeof *f);
        str_copy(f->name, path, sizeof f->name);
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
    f->mtime = time_now();
    version++;
    return true;
}

bool fs_delete(const char *path)
{
    struct file *f = fs_find(path);
    if (!f)
        return false;
    kfree(f->data);
    memset(f, 0, sizeof *f);
    version++;
    return true;
}

bool fs_trash(const char *path, bool trashed)
{
    struct file *f = fs_find(path);
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

int fs_count_in(int folder)
{
    int n = 0;
    for (int i = 0; i < FS_MAX_FILES; i++)
        n += files[i].used && !files[i].trashed && fs_folder_of(files[i].name) == folder;
    return n;
}

static void put(int folder, const char *base, const char *text)
{
    char path[FS_NAME_MAX];
    fs_join(path, sizeof path, folder, base);
    fs_write(path, text, strlen(text), false);
}

void fs_init(void)
{
    put(FOLDER_DOCS, "Добро пожаловать.txt",
        "Добро пожаловать в Safronov OS!\n"
        "\n"
        "Это 64-битная операционная система, написанная с нуля: своё ядро,\n"
        "свои драйверы, свой рабочий стол. Никакого Linux внутри.\n"
        "Она грузится с флешки (BIOS и UEFI) и работает целиком в памяти:\n"
        "файлы живут до перезагрузки.\n"
        "\n"
        "С чего начать:\n"
        "  Пуск (слева внизу) или клавиша Win - все программы и недавние файлы\n"
        "  Ctrl+Пробел - поиск Spotlight, F4 - Launchpad, Alt+Tab - окна\n"
        "  Проводник - папки, файлы и диск\n"
        "  Плеер - мелодии из папки «Музыка» через PC-динамик\n"
        "  Терминал - команды help, ls, cd, neofetch\n"
        "\n"
        "Раскладка: Alt+Shift или Ctrl+Shift.\n");
    put(FOLDER_DOCS, "Список дел.txt",
        "[x] загрузиться\n"
        "[x] не упасть\n"
        "[ ] захватить мир\n");
    put(FOLDER_DOCS, "События.txt",
        "# События для Календаря. Формат: ДД.ММ или ДД.ММ.ГГГГ, потом время\n"
        "# (необязательно) и текст. Без года событие повторяется каждый год.\n"
        "01.01 Новый год\n"
        "07.01 Рождество\n"
        "23.02 День защитника Отечества\n"
        "08.03 Международный женский день\n"
        "12.04 День космонавтики\n"
        "01.05 Праздник весны и труда\n"
        "09.05 День Победы\n"
        "12.06 День России\n"
        "04.11 День народного единства\n"
        "31.12 Канун Нового года\n");
    struct rtc_time t;
    time_split(time_now(), &t);
    char first[96];
    snprintf(first, sizeof first, "%02d.%02d.%04d %02d:%02d Первый запуск Safronov OS\n", t.day, t.month, t.year,
             t.hour, t.min);
    struct file *ev = fs_find("Документы/События.txt");
    if (ev)
        fs_write(ev->name, first, strlen(first), true);
    put(FOLDER_DESKTOP, "Привет.txt",
        "Привет! Этот файл лежит в папке «Рабочий стол», поэтому он виден\n"
        "прямо на рабочем столе. Двойной щелчок открывает, правый - меню.\n");

    /* Melodies for the Player: note/length, "R" is a rest, a dot makes it 1.5x. */
    put(FOLDER_MUSIC, "Коробейники.mel",
        "# Русская народная песня\n"
        "темп 150\n"
        "E5/4 B4/8 C5/8 D5/4 C5/8 B4/8 A4/4 A4/8 C5/8 E5/4 D5/8 C5/8\n"
        "B4/4. C5/8 D5/4 E5/4 C5/4 A4/4 A4/4 R/4\n"
        "R/8 D5/4 F5/8 A5/4 G5/8 F5/8 E5/4. C5/8 E5/4 D5/8 C5/8\n"
        "B4/4 B4/8 C5/8 D5/4 E5/4 C5/4 A4/4 A4/4 R/4\n"
        "E5/4 B4/8 C5/8 D5/4 C5/8 B4/8 A4/4 A4/8 C5/8 E5/4 D5/8 C5/8\n"
        "B4/4. C5/8 D5/4 E5/4 C5/4 A4/4 A4/2\n");
    put(FOLDER_MUSIC, "Ода к радости.mel",
        "# Людвиг ван Бетховен, Симфония № 9\n"
        "темп 132\n"
        "E5 E5 F5 G5 G5 F5 E5 D5 C5 C5 D5 E5 E5/4. D5/8 D5/2\n"
        "E5 E5 F5 G5 G5 F5 E5 D5 C5 C5 D5 E5 D5/4. C5/8 C5/2\n"
        "D5 D5 E5 C5 D5 E5/8 F5/8 E5 C5 D5 E5/8 F5/8 E5 D5 C5 D5 G4/2\n"
        "E5 E5 F5 G5 G5 F5 E5 D5 C5 C5 D5 E5 D5/4. C5/8 C5/2\n");
    put(FOLDER_MUSIC, "К Элизе.mel",
        "# Людвиг ван Бетховен\n"
        "темп 66\n"
        "E5/16 D#5/16 E5/16 D#5/16 E5/16 B4/16 D5/16 C5/16 A4/8 R/16 C4/16 E4/16 A4/16\n"
        "B4/8 R/16 E4/16 G#4/16 B4/16 C5/8 R/16 E4/16\n"
        "E5/16 D#5/16 E5/16 D#5/16 E5/16 B4/16 D5/16 C5/16 A4/8 R/16 C4/16 E4/16 A4/16\n"
        "B4/8 R/16 E4/16 C5/16 B4/16 A4/4\n");
    put(FOLDER_MUSIC, "Турецкий марш.mel",
        "# Вольфганг Амадей Моцарт\n"
        "темп 120\n"
        "B4/16 A4/16 G#4/16 A4/16 C5/8 R/8 D5/16 C5/16 B4/16 C5/16 E5/8 R/8\n"
        "F5/16 E5/16 D#5/16 E5/16 B5/16 A5/16 G#5/16 A5/16 B5/16 A5/16 G#5/16 A5/16 C6/4\n"
        "A5/8 C6/8 B5/8 A5/8 G5/8 A5/8 B5/8 A5/8 G5/8 A5/8 B5/8 A5/8 G5/8 F#5/8 E5/4\n");
    put(FOLDER_MUSIC, "Утро.mel",
        "# Эдвард Григ, «Пер Гюнт»\n"
        "темп 100\n"
        "G5/8 E5/8 D5/8 C5/8 D5/8 E5/8 G5/8 E5/8 D5/8 C5/8 D5/8 E5/16 D5/16\n"
        "E5/8 G5/8 E5/8 G5/8 A5/8 E5/8 A5/8 G5/8 E5/8 D5/8 C5/4.\n");
}
