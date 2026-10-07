#include "fs.h"
#include "ata.h"
#include "vga.h"
#include "task.h"

int str_eq(const char *a, const char *b);

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

static int str_ieq(const char *a, const char *b) {
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return 0;
        a++; b++;
    }
    return to_upper(*a) == to_upper(*b);
}

typedef struct {
    uint8_t jmp[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors;
    uint8_t media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_big;
    uint8_t drive_number;
    uint8_t reserved;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed)) fat_bpb_t;

typedef struct {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t creation_time_ms;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t modification_time;
    uint16_t modification_date;
    uint16_t first_cluster_low;
    uint32_t size;
} __attribute__((packed)) fat_dir_entry_t;

static fat_bpb_t bpb;
static uint32_t fat_start;
static uint32_t root_start;
static uint32_t data_start;
static uint32_t root_sectors;
static int fs_ready = 0;

static uint16_t fat_read_entry(uint16_t cluster) {
    uint32_t fat_offset = cluster + (cluster / 2);
    uint32_t sector = fat_start + fat_offset / FS_SECTOR_SIZE;
    uint32_t offset = fat_offset % FS_SECTOR_SIZE;

    uint8_t buf[FS_SECTOR_SIZE];
    ata_read_sectors(sector, 1, buf);

    uint16_t entry = *(uint16_t*)(buf + offset);
    if (cluster & 1) entry >>= 4;
    else entry &= 0x0FFF;

    return entry;
}

static void fat_write_entry(uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = cluster + (cluster / 2);
    uint8_t buf[FS_SECTOR_SIZE];

    /* Mirror the update into every FAT copy so the volume keeps being a
     * valid FAT12 image, not just for the single copy read back here. */
    for (uint32_t f = 0; f < bpb.num_fats; f++) {
        uint32_t sector = fat_start + f * bpb.sectors_per_fat + fat_offset / FS_SECTOR_SIZE;
        uint32_t offset = fat_offset % FS_SECTOR_SIZE;

        ata_read_sectors(sector, 1, buf);

        uint16_t entry = *(uint16_t*)(buf + offset);
        if (cluster & 1) {
            entry = (entry & 0x000F) | ((value & 0x0FFF) << 4);
        } else {
            entry = (entry & 0xF000) | (value & 0x0FFF);
        }
        *(uint16_t*)(buf + offset) = entry;

        ata_write_sectors(sector, 1, buf);
    }
}

static uint16_t alloc_cluster() {
    uint32_t total_clusters = (bpb.total_sectors - data_start) / bpb.sectors_per_cluster;
    for (uint16_t c = 2; c < total_clusters + 2; c++) {
        if (fat_read_entry(c) == 0) {
            fat_write_entry(c, 0x0FFF);
            return c;
        }
    }
    return 0;
}

static void free_cluster_chain(uint16_t start_cluster) {
    uint16_t cluster = start_cluster;
    while (cluster >= 2 && cluster < 0x0FF8) {
        uint16_t next = fat_read_entry(cluster);
        fat_write_entry(cluster, 0);
        cluster = next;
    }
}

/* Write `size` bytes of `buffer` into the cluster chain, starting `offset`
 * bytes into the file. Clusters are allocated and linked as needed.
 * Returns the number of bytes written, or -1 on failure. */
static int write_cluster_chain(uint16_t start_cluster, const void *buffer,
                               uint32_t size, uint32_t offset,
                               uint16_t *out_last_cluster) {
    const uint8_t *src = (const uint8_t*)buffer;
    uint32_t cluster_bytes = (uint32_t)FS_SECTOR_SIZE * bpb.sectors_per_cluster;
    uint32_t in_cluster = offset;      /* byte offset inside current cluster */
    uint32_t done = 0;                 /* bytes copied out of `buffer` */
    uint16_t cluster = start_cluster;
    uint16_t prev = 0;
    uint16_t last = 0;
    uint8_t sector_buf[FS_SECTOR_SIZE];

    if (size == 0) {
        if (out_last_cluster) *out_last_cluster = last;
        return 0;
    }

    /* Skip whole clusters to reach the one that holds byte `offset`. */
    while (in_cluster >= cluster_bytes) {
        in_cluster -= cluster_bytes;
        prev = cluster;
        cluster = fat_read_entry(cluster);
        if (cluster < 2 || cluster >= 0x0FF8) return -1;
        last = prev;
    }

    while (done < size) {
        if (cluster == 0 || cluster >= 0x0FF8) {
            cluster = alloc_cluster();
            if (cluster == 0) return -1;
            if (prev != 0) fat_write_entry(prev, cluster);
        }

        uint32_t lba = data_start + (cluster - 2) * bpb.sectors_per_cluster;

        for (uint8_t s = 0; s < bpb.sectors_per_cluster && done < size; s++) {
            /* Sectors are read-modify-written so a partial write keeps the
             * bytes around it. */
            uint32_t in_sector = in_cluster % FS_SECTOR_SIZE;
            uint32_t to_copy = FS_SECTOR_SIZE - in_sector;
            if (to_copy > size - done) to_copy = size - done;

            ata_read_sectors(lba + s, 1, sector_buf);
            for (uint32_t i = 0; i < to_copy; i++)
                sector_buf[in_sector + i] = src[done++];
            ata_write_sectors(lba + s, 1, sector_buf);

            in_cluster += to_copy;
        }

        last = cluster;
        prev = cluster;
        if (done >= size) break;
        cluster = fat_read_entry(cluster);
        if (cluster < 2 || cluster >= 0x0FF8) return -1;
        in_cluster = 0;
    }

    if (out_last_cluster) *out_last_cluster = last;
    return (int)size;
}

static int read_cluster_chain(uint16_t start_cluster, void *buffer, uint32_t max_size) {
    uint8_t *buf = (uint8_t*)buffer;
    uint32_t pos = 0;
    uint16_t cluster = start_cluster;
    uint8_t sector_buf[FS_SECTOR_SIZE];

    while (cluster >= 2 && cluster < 0x0FF8) {
        uint32_t lba = data_start + (cluster - 2) * bpb.sectors_per_cluster;

        for (uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            if (ata_read_sectors(lba + s, 1, sector_buf) != 0) return -1;

            uint32_t to_copy = FS_SECTOR_SIZE;
            if (pos + to_copy > max_size) to_copy = max_size - pos;
            for (uint32_t i = 0; i < to_copy; i++) buf[pos++] = sector_buf[i];

            if (pos >= max_size) return (int)pos;
        }

        cluster = fat_read_entry(cluster);
    }

    return (int)pos;
}

void fs_init() {
    uint8_t sector[FS_SECTOR_SIZE];

    if (ata_read_sectors(0, 1, sector) != 0) {
        vga_print("[FS] Failed to read disk.\n");
        fs_ready = 0;
        return;
    }

    fat_bpb_t *b = (fat_bpb_t*)sector;
    bpb = *b;

    if (bpb.bytes_per_sector != FS_SECTOR_SIZE) {
        vga_print("[FS] Unsupported sector size.\n");
        fs_ready = 0;
        return;
    }

    fat_start = bpb.reserved_sectors;
    root_start = fat_start + bpb.num_fats * bpb.sectors_per_fat;
    root_sectors = (bpb.root_entries * 32 + FS_SECTOR_SIZE - 1) / FS_SECTOR_SIZE;
    data_start = root_start + root_sectors;

    vga_print("[FS] FAT12 ready. ");
    print_dec(bpb.root_entries);
    vga_print(" root entries.\n");
    fs_ready = 1;
}

int fs_open(const char *path, int flags) {
    (void)flags;
    if (!fs_ready) return -1;

    uint8_t root[FS_SECTOR_SIZE * 14];
    for (uint32_t i = 0; i < root_sectors; i++) {
        ata_read_sectors(root_start + i, 1, root + i * FS_SECTOR_SIZE);
    }

    fat_dir_entry_t *entries = (fat_dir_entry_t*)root;

    for (int i = 0; i < bpb.root_entries; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].name[0] == 0x08) continue;

        char name[13];
        int p = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
            name[p++] = entries[i].name[j];
        }
        if (entries[i].name[8] != ' ') {
            name[p++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) {
                name[p++] = entries[i].name[j];
            }
        }
        name[p] = '\0';

        if (str_ieq(path, name)) return i;
    }

    return -1;
}

uint32_t fs_size(int fd) {
    if (!fs_ready || fd < 0) return 0;
    uint8_t sector[FS_SECTOR_SIZE];
    ata_read_sectors(root_start + (fd * 32) / FS_SECTOR_SIZE, 1, sector);
    fat_dir_entry_t *e = &((fat_dir_entry_t*)sector)[fd % 16];
    return e->size;
}

int fs_read(int fd, void *buffer, uint32_t size) {
    if (!fs_ready || fd < 0 || !buffer) return -1;

    uint8_t sector[FS_SECTOR_SIZE];
    ata_read_sectors(root_start + (fd * 32) / FS_SECTOR_SIZE, 1, sector);
    fat_dir_entry_t *e = &((fat_dir_entry_t*)sector)[fd % 16];

    uint16_t start = e->first_cluster_low;
    uint32_t file_size = e->size;
    if (size > file_size) size = file_size;

    return read_cluster_chain(start, buffer, size);
}

void fs_list() {
    if (!fs_ready) {
        vga_print("[FS] Not initialized.\n");
        return;
    }

    uint8_t root[FS_SECTOR_SIZE * 14];
    for (uint32_t i = 0; i < root_sectors; i++) {
        ata_read_sectors(root_start + i, 1, root + i * FS_SECTOR_SIZE);
    }

    fat_dir_entry_t *entries = (fat_dir_entry_t*)root;
    int count = 0;

    for (int i = 0; i < bpb.root_entries; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].name[0] == 0x08) continue;

        char name[13];
        int p = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
            name[p++] = entries[i].name[j];
        }
        if (entries[i].name[8] != ' ') {
            name[p++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) {
                name[p++] = entries[i].name[j];
            }
        }
        name[p] = '\0';

        if (entries[i].attributes & 0x10) {
            vga_print_color(name, 0x0B);
            vga_print("/ ");
        } else {
            vga_print_color(name, 0x0F);
            vga_print("  ");
            print_dec(entries[i].size);
            vga_print(" bytes");
            vga_print("  ");
        }
        count++;
    }

    if (count == 0) vga_print("(empty)");
    vga_print("\n");
}

void fs_cat(const char *path) {
    if (!fs_ready) {
        vga_print("[FS] Not initialized.\n");
        return;
    }

    int fd = fs_open(path, 0);
    if (fd < 0) {
        vga_print("File not found: ");
        vga_print(path);
        vga_print("\n");
        return;
    }

    uint32_t size = fs_size(fd);
    if (size == 0) {
        vga_print("(empty file)\n");
        return;
    }

    uint8_t buf[FS_SECTOR_SIZE * 8];
    uint32_t to_read = size;
    if (to_read > sizeof(buf) - 1) to_read = sizeof(buf) - 1;

    int read = fs_read(fd, buf, to_read);
    if (read > 0) {
        buf[read] = '\0';
        vga_print((char*)buf);
    }
    if (read >= 0 && size > sizeof(buf) - 1) {
        vga_print("\n... (truncated)\n");
    } else {
        vga_print("\n");
    }
}

/* Build the padded 11-byte FAT 8.3 short name for path. The base field is
 * truncated to 8 characters and the extension to 3; the caller passes an
 * 11-byte buffer which is fully overwritten. */
static void make_short_name(const char *path, uint8_t out[11]) {
    int i;

    for (i = 0; i < 11; i++) out[i] = ' ';

    i = 0;
    while (path[i] && path[i] != '.' && i < 8) {
        out[i] = (uint8_t)to_upper(path[i]);
        i++;
    }
    while (path[i] && path[i] != '.') i++;   /* drop over-long base */
    if (path[i] != '.') return;

    i++;
    for (int e = 0; e < 3 && path[i + e]; e++)
        out[8 + e] = (uint8_t)to_upper(path[i + e]);
}

int fs_create(const char *path) {
    if (!fs_ready) return -1;

    uint8_t root[FS_SECTOR_SIZE * 14];
    for (uint32_t i = 0; i < root_sectors; i++) {
        if (ata_read_sectors(root_start + i, 1, root + i * FS_SECTOR_SIZE) != 0)
            return -1;
    }

    fat_dir_entry_t *entries = (fat_dir_entry_t*)root;
    int slot = -1;
    for (int i = 0; i < bpb.root_entries; i++) {
        uint8_t first = entries[i].name[0];
        if (first == 0x00 || first == 0xE5) { slot = i; break; }
    }
    if (slot < 0) return -1;

    /* Stage the entry in its own sector. Writing a whole FS_SECTOR_SIZE
     * block from an unaligned offset in `root` would spill the following
     * directory entries over this one. */
    uint32_t sector_idx = root_start + (uint32_t)(slot * 32) / FS_SECTOR_SIZE;
    uint32_t offset     = (uint32_t)(slot * 32) % FS_SECTOR_SIZE;

    uint8_t sector[FS_SECTOR_SIZE];
    if (ata_read_sectors(sector_idx, 1, sector) != 0) return -1;

    fat_dir_entry_t e;
    memset(&e, 0, sizeof(e));
    make_short_name(path, e.name);
    e.attributes = 0x20;              /* archive: regular file */
    e.creation_time = 0x4000;
    e.creation_date = 0x21;
    e.access_date = 0x21;
    e.modification_time = 0x4000;
    e.modification_date = 0x21;
    e.first_cluster_low = 0;
    e.size = 0;
    memcpy(sector + offset, &e, sizeof(e));

    if (ata_write_sectors(sector_idx, 1, sector) != 0) return -1;
    return slot;
}

int fs_write_at(int fd, const void *buffer, uint32_t size, uint32_t offset) {
    if (!fs_ready || fd < 0 || !buffer) return -1;

    uint8_t sector[FS_SECTOR_SIZE];
    uint32_t sector_idx = root_start + (fd * 32) / FS_SECTOR_SIZE;
    uint32_t entry_idx = fd % 16;

    ata_read_sectors(sector_idx, 1, sector);
    fat_dir_entry_t *e = &((fat_dir_entry_t*)sector)[entry_idx];

    uint16_t start = e->first_cluster_low;
    uint16_t last = 0;

    if (start == 0) {
        /* An empty file can only be written from its beginning. */
        if (offset != 0) return -1;
        start = alloc_cluster();
        if (start == 0) return -1;
        e->first_cluster_low = start;
    }

    int written = write_cluster_chain(start, buffer, size, offset, &last);
    if (written < 0) return -1;

    /* Terminate the chain, and release clusters the new length no longer
     * covers -- a shorter overwrite truncates the file. */
    if (last != 0 && last != 0x0FFF) {
        uint16_t next = fat_read_entry(last);
        if (next != 0x0FFF && next != 0) {
            free_cluster_chain(next);
        }
        fat_write_entry(last, 0x0FFF);
    }

    e->size = offset + written;
    ata_write_sectors(sector_idx, 1, sector);

    return written;
}

int fs_write(int fd, const void *buffer, uint32_t size) {
    return fs_write_at(fd, buffer, size, 0);
}

int fs_append(int fd, const void *buffer, uint32_t size) {
    return fs_write_at(fd, buffer, size, fs_size(fd));
}

int fs_delete(const char *path) {
    if (!fs_ready) return -1;

    int fd = fs_open(path, 0);
    if (fd < 0) return -1;

    uint8_t sector[FS_SECTOR_SIZE];
    uint32_t sector_idx = root_start + (fd * 32) / FS_SECTOR_SIZE;
    uint32_t entry_idx = fd % 16;

    ata_read_sectors(sector_idx, 1, sector);
    fat_dir_entry_t *e = &((fat_dir_entry_t*)sector)[entry_idx];

    if (e->first_cluster_low != 0) {
        free_cluster_chain(e->first_cluster_low);
    }

    e->name[0] = 0xE5;
    for (int j = 1; j < 11; j++) e->name[j] = ' ';

    ata_write_sectors(sector_idx, 1, sector);
    return 0;
}

/* Compatibility shims for the extended FD API (fs.h).
 * Current FAT12 backend uses the directory index as the fd and always
 * reads/writes from the start of the file, so close/seek/tell are trivial.
 * A full open-file table with per-fd positions can replace these later. */
int fs_close(int fd) {
    (void)fd;
    return 0;
}

int fs_seek(int fd, int offset, int whence) {
    (void)fd; (void)offset; (void)whence;
    return 0;
}

uint32_t fs_tell(int fd) {
    (void)fd;
    return 0;
}
