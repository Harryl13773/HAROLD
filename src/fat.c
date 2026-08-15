// FAT16 filesystem driver: boot sector parsing, directory/LFN traversal, and file read/write

#include <stdint.h>
#include "ata.h"
#include "terminal.h"
#include "task.h"
#include "io.h"
#include "fat.h"

// Boot sector layout: BPB + FAT16 Extended BPB, byte-for-byte per spec
struct fat_boot_sector
{
    uint8_t jump[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sector_count;
    uint8_t num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t media_descriptor;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed));

// One 32-byte root directory entry, classic 8.3 format
struct fat_dir_entry
{
    uint8_t name[8];
    uint8_t ext[3];
    uint8_t attr;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high; // unused on FAT16
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

// One 32-byte LFN entry — old DOS tools skip these via attr==0x0F, we now actually read them
struct fat_lfn_entry
{
    uint8_t sequence;       // ordinal (1-based), 0x40 bit set on the first one encountered
    uint16_t name1[5];      // chars 0-4, UTF-16LE
    uint8_t attr;           // always 0x0F
    uint8_t type;           // always 0 for VFAT long names
    uint8_t checksum;       // checksum of the short entry this belongs to
    uint16_t name2[6];      // chars 5-10, UTF-16LE
    uint16_t first_cluster; // always 0, unused
    uint16_t name3[2];      // chars 11-12, UTF-16LE
} __attribute__((packed));

#define FAT_ATTR_VOLUME_LABEL 0x08
#define FAT_ATTR_LONG_NAME 0x0F

#define LFN_MAX_CHARS 256 // FAT's own cap is 255 characters; this is a safe round bound

// Reconstructed long name, rebuilt fresh for each short entry — placed by ordinal since entries are stored in reverse
static char lfn_buffer[LFN_MAX_CHARS];
static int lfn_has_data = 0;
static uint8_t lfn_checksum = 0;
static int lfn_checksum_valid = 0;

// Layout computed once by fat_init() from the BPB
static uint32_t fat_start_lba;
static uint32_t root_dir_start_lba;
static uint32_t root_dir_sectors;
static uint32_t data_start_lba;
static uint16_t root_entry_count;
static uint8_t sectors_per_cluster;
static uint8_t num_fats;
static uint32_t fat_size_sectors;
static int fat_ready = 0;

// One open file's read (or write) position, persisted across calls
struct open_file
{
    int in_use;
    uint32_t file_size;
    uint16_t current_cluster;
    uint32_t cluster_offset; // byte offset within current_cluster
    uint32_t file_offset;    // absolute byte offset within the file
    int writable;
    uint32_t dir_entry_sector; // where this file's directory entry lives — write mode updates it directly
    uint32_t dir_entry_offset; // index of the entry within that sector
    int owner_task_id;         // whoever opened this — lets task_reap close it if they exit without doing so
};

#define MAX_OPEN_FILES 8
#define FAT_FD_BASE 3 // 0/1/2 stay reserved for stdin/stdout/stderr
static struct open_file open_files[MAX_OPEN_FILES];

// Converts "readme.txt" into the padded 11-byte 8.3 directory format
static void fat_name_to_83(const char *filename, uint8_t *out11)
{
    for (int i = 0; i < 11; i++)
    {
        out11[i] = ' ';
    }

    int i = 0;
    int out_i = 0;
    while (filename[i] != '\0' && filename[i] != '.' && out_i < 8)
    {
        char c = filename[i];
        if (c >= 'a' && c <= 'z')
        {
            c = (char)(c - 32);
        }
        out11[out_i++] = (uint8_t)c;
        i++;
    }

    while (filename[i] != '\0' && filename[i] != '.')
    {
        i++;
    }

    if (filename[i] == '.')
    {
        i++;
        int ext_i = 8;
        while (filename[i] != '\0' && ext_i < 11)
        {
            char c = filename[i];
            if (c >= 'a' && c <= 'z')
            {
                c = (char)(c - 32);
            }
            out11[ext_i++] = (uint8_t)c;
            i++;
        }
    }
}

// Compares a directory entry's name+ext against a pre-formatted 8.3 name
static int fat_dir_entry_matches(const struct fat_dir_entry *entry, const uint8_t *name83)
{
    for (int i = 0; i < 8; i++)
    {
        if (entry->name[i] != name83[i])
        {
            return 0;
        }
    }
    for (int i = 0; i < 3; i++)
    {
        if (entry->ext[i] != name83[8 + i])
        {
            return 0;
        }
    }
    return 1;
}

// Clears any in-progress long-name reconstruction — called between entries
static void lfn_reset(void)
{
    for (int i = 0; i < LFN_MAX_CHARS; i++)
    {
        lfn_buffer[i] = '\0';
    }
    lfn_has_data = 0;
    lfn_checksum_valid = 0;
}

// Folds 13 UTF-16LE characters from one LFN entry into lfn_buffer at its ordinal's position
static void lfn_accumulate(const struct fat_lfn_entry *entry)
{
    int ordinal = entry->sequence & 0x1F; // mask off the "last entry" flag bit
    int base = (ordinal - 1) * 13;

    if (ordinal < 1 || base + 13 > LFN_MAX_CHARS - 1)
    {
        return; // malformed or oversized — ignore defensively rather than overrun the buffer
    }

    if (!lfn_has_data)
    {
        lfn_checksum = entry->checksum;
        lfn_checksum_valid = 1;
    }
    else if (entry->checksum != lfn_checksum)
    {
        lfn_checksum_valid = 0; // entries disagree on which short name they belong to
    }

    uint16_t chars[13];
    for (int i = 0; i < 5; i++)
    {
        chars[i] = entry->name1[i];
    }
    for (int i = 0; i < 6; i++)
    {
        chars[5 + i] = entry->name2[i];
    }
    for (int i = 0; i < 2; i++)
    {
        chars[11 + i] = entry->name3[i];
    }

    for (int i = 0; i < 13; i++)
    {
        uint16_t c = chars[i];
        if (c == 0x0000)
        {
            lfn_buffer[base + i] = '\0';
            break;
        }
        if (c == 0xFFFF)
        {
            break; // padding past the end of a shorter name
        }
        lfn_buffer[base + i] = (c < 128) ? (char)c : '?'; // no real Unicode support, ASCII only
    }

    lfn_has_data = 1;
}

// Computes the standard FAT short-name checksum, used to validate an LFN group
static uint8_t fat_short_name_checksum(const struct fat_dir_entry *e)
{
    uint8_t name11[11];
    for (int i = 0; i < 8; i++)
    {
        name11[i] = e->name[i];
    }
    for (int i = 0; i < 3; i++)
    {
        name11[8 + i] = e->ext[i];
    }

    uint8_t sum = 0;
    for (int i = 0; i < 11; i++)
    {
        sum = (uint8_t)(((sum & 1) ? 0x80 : 0) + (sum >> 1) + name11[i]);
    }
    return sum;
}

// Case-insensitive comparison, for matching against a reconstructed long name
static int names_equal_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0')
    {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z')
        {
            ca = (char)(ca - 32);
        }
        if (cb >= 'a' && cb <= 'z')
        {
            cb = (char)(cb - 32);
        }
        if (ca != cb)
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

int fat_init(void)
{
    uint8_t boot[ATA_SECTOR_SIZE];
    if (ata_read_sector(0, boot) != 0)
    {
        terminal_writestring("FAT: could not read boot sector\n");
        return -1;
    }

    if (boot[510] != 0x55 || boot[511] != 0xAA)
    {
        terminal_writestring("FAT: no boot signature, not a FAT volume\n");
        return -1;
    }

    struct fat_boot_sector *bpb = (struct fat_boot_sector *)boot;

    if (bpb->bytes_per_sector != ATA_SECTOR_SIZE)
    {
        terminal_writestring("FAT: unsupported sector size\n");
        return -1;
    }

    // fs_type is just a label string, but it's good enough for a first pass
    if (bpb->fs_type[0] != 'F' || bpb->fs_type[1] != 'A' || bpb->fs_type[2] != 'T' ||
        bpb->fs_type[3] != '1' || bpb->fs_type[4] != '6')
    {
        terminal_writestring("FAT: not FAT16 (or fs_type field unset)\n");
        return -1;
    }

    sectors_per_cluster = bpb->sectors_per_cluster;
    root_entry_count = bpb->root_entry_count;
    num_fats = bpb->num_fats;
    fat_size_sectors = bpb->fat_size_16;

    fat_start_lba = bpb->reserved_sector_count;
    root_dir_start_lba = fat_start_lba + (uint32_t)bpb->num_fats * bpb->fat_size_16;
    root_dir_sectors = ((uint32_t)root_entry_count * 32 + (ATA_SECTOR_SIZE - 1)) / ATA_SECTOR_SIZE;
    data_start_lba = root_dir_start_lba + root_dir_sectors;

    fat_ready = 1;

    terminal_writestring("FAT: FAT16 volume detected, ");
    terminal_print_dec(root_entry_count);
    terminal_writestring(" root entries, ");
    terminal_print_dec(sectors_per_cluster);
    terminal_writestring(" sectors/cluster\n");

    return 0;
}

// Scans the root directory for filename, checking both the reconstructed long name and the 8.3 short
// name, and reports exactly where the match lives on disk (sector + index within that sector) —
// overwrite/append need this; plain reads only need the entry's contents, via the wrapper below
static int fat_find_entry_location(const char *filename, uint32_t *out_sector, uint32_t *out_offset, struct fat_dir_entry *out_entry)
{
    uint8_t name83[11];
    fat_name_to_83(filename, name83);

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    int entries_per_sector = ATA_SECTOR_SIZE / sizeof(struct fat_dir_entry);

    lfn_reset();

    for (uint32_t s = 0; s < root_dir_sectors; s++)
    {
        if (ata_read_sector(root_dir_start_lba + s, sector_buf) != 0)
        {
            return -1;
        }

        struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;

        for (int i = 0; i < entries_per_sector; i++)
        {
            struct fat_dir_entry *e = &entries[i];

            if (e->name[0] == 0x00)
            {
                return -1; // end of directory
            }
            if (e->name[0] == 0xE5)
            {
                lfn_reset(); // a deleted entry breaks any LFN group pointing at it
                continue;
            }
            if (e->attr == FAT_ATTR_LONG_NAME)
            {
                lfn_accumulate((const struct fat_lfn_entry *)e);
                continue;
            }
            if (e->attr & FAT_ATTR_VOLUME_LABEL)
            {
                lfn_reset();
                continue;
            }

            int matched = fat_dir_entry_matches(e, name83);

            if (!matched && lfn_has_data && lfn_checksum_valid &&
                fat_short_name_checksum(e) == lfn_checksum)
            {
                matched = names_equal_ci(filename, lfn_buffer);
            }

            lfn_reset();

            if (matched)
            {
                *out_sector = root_dir_start_lba + s;
                *out_offset = (uint32_t)i;
                *out_entry = *e;
                return 0;
            }
        }
    }

    return -1;
}

// Convenience wrapper for callers that only need an entry's contents, not where it lives on disk
static int fat_find_entry(const char *filename, struct fat_dir_entry *out_entry)
{
    uint32_t sector, offset;
    return fat_find_entry_location(filename, &sector, &offset, out_entry);
}

// Looks up the next cluster in a chain via the FAT
static uint16_t fat_get_next_cluster(uint16_t cluster)
{
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t fat_sector = fat_start_lba + (fat_offset / ATA_SECTOR_SIZE);
    uint32_t offset_in_sector = fat_offset % ATA_SECTOR_SIZE;

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    if (ata_read_sector(fat_sector, sector_buf) != 0)
    {
        return 0xFFFF; // fail safe: treat a read error as end-of-chain
    }

    uint16_t *entries = (uint16_t *)sector_buf;
    return entries[offset_in_sector / 2];
}

int fat_read_file(const char *filename, uint8_t *buffer, uint32_t buffer_size)
{
    if (!fat_ready)
    {
        terminal_writestring("FAT: no filesystem mounted\n");
        return -1;
    }

    struct fat_dir_entry entry;
    if (fat_find_entry(filename, &entry) != 0)
    {
        terminal_writestring("FAT: file not found: ");
        terminal_writestring(filename);
        terminal_writestring("\n");
        return -1;
    }

    if (entry.file_size > buffer_size)
    {
        terminal_writestring("FAT: buffer too small for file\n");
        return -1;
    }

    uint32_t bytes_read = 0;
    uint16_t cluster = entry.first_cluster_low;
    uint8_t sector_buf[ATA_SECTOR_SIZE];

    // 0xFFF8-0xFFFF marks end-of-chain; below 2 is not a valid cluster
    while (cluster >= 2 && cluster < 0xFFF8 && bytes_read < entry.file_size)
    {
        uint32_t cluster_lba = data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster;

        for (uint32_t s = 0; s < sectors_per_cluster && bytes_read < entry.file_size; s++)
        {
            if (ata_read_sector(cluster_lba + s, sector_buf) != 0)
            {
                return (int)bytes_read;
            }

            uint32_t remaining = entry.file_size - bytes_read;
            uint32_t to_copy = remaining < ATA_SECTOR_SIZE ? remaining : ATA_SECTOR_SIZE;

            for (uint32_t i = 0; i < to_copy; i++)
            {
                buffer[bytes_read + i] = sector_buf[i];
            }
            bytes_read += to_copy;
        }

        cluster = fat_get_next_cluster(cluster);
    }

    return (int)bytes_read;
}

// Opens filename for reading, returns a real fd (>= FAT_FD_BASE) or -1
int fat_open(const char *filename)
{
    if (!fat_ready)
    {
        return -1;
    }

    struct fat_dir_entry entry;
    if (fat_find_entry(filename, &entry) != 0)
    {
        return -1;
    }

    // The slot scan-and-claim below must be atomic: without this, a timer interrupt could switch
    // to another task (e.g. task_reap running fat_close_all_for_task) mid-scan and corrupt the
    // shared table — the same category of bug heap.c's kmalloc/kfree once had, fixed the same way
    uint32_t flags = save_and_disable_interrupts();

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!open_files[i].in_use)
        {
            open_files[i].in_use = 1;
            open_files[i].file_size = entry.file_size;
            open_files[i].current_cluster = entry.first_cluster_low;
            open_files[i].cluster_offset = 0;
            open_files[i].file_offset = 0;
            open_files[i].writable = 0;
            open_files[i].owner_task_id = task_current_id();
            restore_interrupts(flags);
            return i + FAT_FD_BASE;
        }
    }

    restore_interrupts(flags);
    return -1; // no free descriptor slots
}

// Reads up to len bytes from fd, resuming from the last position across calls, unlike fat_read_file
int fat_read(int fd, uint8_t *buffer, uint32_t len)
{
    int index = fd - FAT_FD_BASE;
    if (index < 0 || index >= MAX_OPEN_FILES || !open_files[index].in_use)
    {
        return -1;
    }

    struct open_file *f = &open_files[index];
    uint32_t cluster_size = (uint32_t)sectors_per_cluster * ATA_SECTOR_SIZE;
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    uint32_t bytes_read = 0;

    while (bytes_read < len && f->file_offset < f->file_size &&
           f->current_cluster >= 2 && f->current_cluster < 0xFFF8)
    {
        uint32_t sector_in_cluster = f->cluster_offset / ATA_SECTOR_SIZE;
        uint32_t offset_in_sector = f->cluster_offset % ATA_SECTOR_SIZE;
        uint32_t cluster_lba = data_start_lba + (uint32_t)(f->current_cluster - 2) * sectors_per_cluster;

        if (ata_read_sector(cluster_lba + sector_in_cluster, sector_buf) != 0)
        {
            return (int)bytes_read; // return whatever succeeded before the error
        }

        uint32_t available_in_sector = ATA_SECTOR_SIZE - offset_in_sector;
        uint32_t remaining_in_file = f->file_size - f->file_offset;
        uint32_t remaining_requested = len - bytes_read;

        uint32_t to_copy = available_in_sector;
        if (remaining_in_file < to_copy)
        {
            to_copy = remaining_in_file;
        }
        if (remaining_requested < to_copy)
        {
            to_copy = remaining_requested;
        }

        for (uint32_t i = 0; i < to_copy; i++)
        {
            buffer[bytes_read + i] = sector_buf[offset_in_sector + i];
        }

        bytes_read += to_copy;
        f->file_offset += to_copy;
        f->cluster_offset += to_copy;

        if (f->cluster_offset >= cluster_size)
        {
            f->current_cluster = fat_get_next_cluster(f->current_cluster);
            f->cluster_offset = 0;
        }
    }

    return (int)bytes_read; // 0 means EOF, distinct from -1 (invalid fd)
}

// Closes a descriptor opened by fat_open or fat_open_write
int fat_close(int fd)
{
    int index = fd - FAT_FD_BASE;
    if (index < 0 || index >= MAX_OPEN_FILES || !open_files[index].in_use)
    {
        return -1;
    }

    uint32_t flags = save_and_disable_interrupts();
    open_files[index].in_use = 0;
    restore_interrupts(flags);
    return 0;
}

// Closes every descriptor still owned by task_id — called by task_reap so a task that exits
// without calling close() itself doesn't leak a slot out of MAX_OPEN_FILES forever
void fat_close_all_for_task(int task_id)
{
    uint32_t flags = save_and_disable_interrupts();

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (open_files[i].in_use && open_files[i].owner_task_id == task_id)
        {
            open_files[i].in_use = 0;
        }
    }

    restore_interrupts(flags);
}

// Writes a 16-bit FAT entry at the given cluster index, updating every FAT copy so they stay in sync
static void fat_set_cluster_entry(uint16_t cluster, uint16_t value)
{
    uint32_t fat_offset = (uint32_t)cluster * 2;
    uint32_t sector_offset = fat_offset / ATA_SECTOR_SIZE;
    uint32_t offset_in_sector = fat_offset % ATA_SECTOR_SIZE;

    uint8_t sector_buf[ATA_SECTOR_SIZE];

    for (uint8_t copy = 0; copy < num_fats; copy++)
    {
        uint32_t sector = fat_start_lba + (uint32_t)copy * fat_size_sectors + sector_offset;
        if (ata_read_sector(sector, sector_buf) != 0)
        {
            continue; // best-effort — a read failure here shouldn't abort the whole write
        }
        uint16_t *entries = (uint16_t *)sector_buf;
        entries[offset_in_sector / 2] = value;
        ata_write_sector(sector, sector_buf);
    }
}

// Finds the first free cluster (a zero FAT entry), marks it end-of-chain to claim it, and returns it;
// returns 0 if the disk is full
static uint16_t fat_alloc_cluster(void)
{
    uint32_t total_entries = fat_size_sectors * (ATA_SECTOR_SIZE / 2);
    uint8_t sector_buf[ATA_SECTOR_SIZE];

    for (uint32_t cluster = 2; cluster < total_entries; cluster++)
    {
        uint32_t fat_offset = cluster * 2;
        uint32_t sector = fat_start_lba + (fat_offset / ATA_SECTOR_SIZE);
        uint32_t offset_in_sector = fat_offset % ATA_SECTOR_SIZE;

        if (offset_in_sector == 0) // only re-read when crossing into a new sector
        {
            if (ata_read_sector(sector, sector_buf) != 0)
            {
                return 0;
            }
        }

        uint16_t *entries = (uint16_t *)sector_buf;
        if (entries[offset_in_sector / 2] == 0x0000)
        {
            fat_set_cluster_entry((uint16_t)cluster, 0xFFFF); // claim it immediately as end-of-chain
            return (uint16_t)cluster;
        }
    }

    return 0; // disk full
}

// Finds the first free (deleted or never-used) root directory slot
static int fat_alloc_dir_entry(uint32_t *out_sector, uint32_t *out_offset)
{
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    int entries_per_sector = ATA_SECTOR_SIZE / sizeof(struct fat_dir_entry);

    for (uint32_t s = 0; s < root_dir_sectors; s++)
    {
        if (ata_read_sector(root_dir_start_lba + s, sector_buf) != 0)
        {
            return -1;
        }

        struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;

        for (int i = 0; i < entries_per_sector; i++)
        {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5)
            {
                *out_sector = root_dir_start_lba + s;
                *out_offset = (uint32_t)i;
                return 0;
            }
        }
    }

    return -1; // root directory full — FAT16's root dir has a fixed size, unlike subdirectories
}

// Walks a cluster chain and returns its last cluster, or 0 if the chain is empty
static uint16_t fat_find_last_cluster(uint16_t first_cluster)
{
    if (first_cluster < 2 || first_cluster >= 0xFFF8)
    {
        return 0;
    }

    uint16_t cluster = first_cluster;
    uint16_t next = fat_get_next_cluster(cluster);
    while (next >= 2 && next < 0xFFF8)
    {
        cluster = next;
        next = fat_get_next_cluster(cluster);
    }
    return cluster;
}

// Frees every cluster in a chain back to the FAT — used when truncating an existing file
static void fat_free_chain(uint16_t first_cluster)
{
    uint16_t cluster = first_cluster;
    while (cluster >= 2 && cluster < 0xFFF8)
    {
        uint16_t next = fat_get_next_cluster(cluster);
        fat_set_cluster_entry(cluster, 0x0000);
        cluster = next;
    }
}

// Creates filename, or opens an existing one per mode; returns a fd usable with fat_write, or -1
int fat_open_write(const char *filename, int mode)
{
    if (!fat_ready)
    {
        return -1;
    }

    uint32_t dir_sector, dir_offset;
    struct fat_dir_entry existing;
    int exists = (fat_find_entry_location(filename, &dir_sector, &dir_offset, &existing) == 0);

    if (exists && mode == FAT_OPEN_CREATE)
    {
        return -1; // strict create — caller must ask for TRUNCATE or APPEND to touch an existing file
    }
    if (!exists && fat_alloc_dir_entry(&dir_sector, &dir_offset) != 0)
    {
        return -1; // root directory full
    }

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    if (ata_read_sector(dir_sector, sector_buf) != 0)
    {
        return -1;
    }
    struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;
    struct fat_dir_entry *e = &entries[dir_offset];

    uint16_t start_cluster = 0;
    uint32_t start_file_size = 0;
    uint32_t start_cluster_offset = 0;

    if (!exists) // a brand new zero-length entry, same as the old create-only behavior
    {
        uint8_t name83[11];
        fat_name_to_83(filename, name83);
        for (int i = 0; i < 8; i++)
        {
            e->name[i] = name83[i];
        }
        for (int i = 0; i < 3; i++)
        {
            e->ext[i] = name83[8 + i];
        }
        e->attr = 0;
        e->reserved = 0;
        e->create_time_tenth = 0;
        e->create_time = 0;
        e->create_date = 0;
        e->last_access_date = 0;
        e->first_cluster_high = 0;
        e->write_time = 0;
        e->write_date = 0;
        e->first_cluster_low = 0;
        e->file_size = 0;

        if (ata_write_sector(dir_sector, sector_buf) != 0)
        {
            return -1;
        }
    }
    else if (mode == FAT_OPEN_TRUNCATE)
    {
        if (existing.first_cluster_low != 0)
        {
            fat_free_chain(existing.first_cluster_low); // return the old data's clusters before rewriting
        }

        e->first_cluster_low = 0;
        e->file_size = 0;
        if (ata_write_sector(dir_sector, sector_buf) != 0)
        {
            return -1;
        }
    }
    else // FAT_OPEN_APPEND — resume writing from wherever the file currently ends
    {
        start_file_size = existing.file_size;

        if (existing.first_cluster_low != 0)
        {
            start_cluster = fat_find_last_cluster(existing.first_cluster_low);

            uint32_t cluster_size = (uint32_t)sectors_per_cluster * ATA_SECTOR_SIZE;
            start_cluster_offset = start_file_size % cluster_size;
            if (start_cluster_offset == 0 && start_file_size > 0)
            {
                // the last cluster is exactly full — force fat_write's own overflow check to
                // allocate a fresh cluster before the very next byte, rather than writing offset 0
                // of the (already full) last cluster and silently overwriting real data
                start_cluster_offset = cluster_size;
            }
        }
    }

    uint32_t flags = save_and_disable_interrupts(); // same shared-table race as fat_open — see the comment there

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!open_files[i].in_use)
        {
            open_files[i].in_use = 1;
            open_files[i].file_size = start_file_size;
            open_files[i].current_cluster = start_cluster; // 0 means "allocate lazily on first write"
            open_files[i].cluster_offset = start_cluster_offset;
            open_files[i].file_offset = start_file_size;
            open_files[i].writable = 1;
            open_files[i].dir_entry_sector = dir_sector;
            open_files[i].dir_entry_offset = dir_offset;
            open_files[i].owner_task_id = task_current_id();
            restore_interrupts(flags);
            return i + FAT_FD_BASE;
        }
    }

    restore_interrupts(flags);
    return -1; // no free descriptor slots
}

// Writes len bytes from buffer to fd, extending the file's cluster chain as needed
int fat_write(int fd, const uint8_t *buffer, uint32_t len)
{
    int index = fd - FAT_FD_BASE;
    if (index < 0 || index >= MAX_OPEN_FILES || !open_files[index].in_use || !open_files[index].writable)
    {
        return -1;
    }

    struct open_file *f = &open_files[index];
    uint32_t cluster_size = (uint32_t)sectors_per_cluster * ATA_SECTOR_SIZE;
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    uint32_t bytes_written = 0;

    while (bytes_written < len)
    {
        if (f->current_cluster == 0) // this is the very first write — nothing allocated yet
        {
            uint16_t new_cluster = fat_alloc_cluster();
            if (new_cluster == 0)
            {
                break; // disk full
            }
            f->current_cluster = new_cluster;
            f->cluster_offset = 0;

            if (ata_read_sector(f->dir_entry_sector, sector_buf) == 0) // record it as the file's first cluster
            {
                struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;
                entries[f->dir_entry_offset].first_cluster_low = new_cluster;
                ata_write_sector(f->dir_entry_sector, sector_buf);
            }
        }
        else if (f->cluster_offset >= cluster_size) // current cluster is full — chain to a new one
        {
            uint16_t new_cluster = fat_alloc_cluster();
            if (new_cluster == 0)
            {
                break; // disk full
            }
            fat_set_cluster_entry(f->current_cluster, new_cluster);
            f->current_cluster = new_cluster;
            f->cluster_offset = 0;
        }

        uint32_t sector_in_cluster = f->cluster_offset / ATA_SECTOR_SIZE;
        uint32_t offset_in_sector = f->cluster_offset % ATA_SECTOR_SIZE;
        uint32_t cluster_lba = data_start_lba + (uint32_t)(f->current_cluster - 2) * sectors_per_cluster;

        if (ata_read_sector(cluster_lba + sector_in_cluster, sector_buf) != 0) // read-modify-write
        {
            break;
        }

        uint32_t available_in_sector = ATA_SECTOR_SIZE - offset_in_sector;
        uint32_t remaining_requested = len - bytes_written;
        uint32_t to_copy = available_in_sector < remaining_requested ? available_in_sector : remaining_requested;

        for (uint32_t i = 0; i < to_copy; i++)
        {
            sector_buf[offset_in_sector + i] = buffer[bytes_written + i];
        }

        if (ata_write_sector(cluster_lba + sector_in_cluster, sector_buf) != 0)
        {
            break;
        }

        bytes_written += to_copy;
        f->file_offset += to_copy;
        f->cluster_offset += to_copy;
        if (f->file_offset > f->file_size)
        {
            f->file_size = f->file_offset;
        }
    }

    // Persist the updated size now, not just on close — so a task that exits without calling
    // fat_close (already a documented leak) still leaves a correctly-sized file on disk
    if (ata_read_sector(f->dir_entry_sector, sector_buf) == 0)
    {
        struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;
        entries[f->dir_entry_offset].file_size = f->file_size;
        ata_write_sector(f->dir_entry_sector, sector_buf);
    }

    return (int)bytes_written;
}

// Bounded copy that always null-terminates, unlike strncpy
static void fat_copy_string(char *dest, const char *src, uint32_t dest_size)
{
    uint32_t i = 0;
    while (src[i] != '\0' && i < dest_size - 1)
    {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

// Converts a raw 8.3 entry into a readable "NAME.EXT" string, trimming FAT's space-padding
static void fat_short_name_to_display(const struct fat_dir_entry *e, char *out, uint32_t out_size)
{
    uint32_t pos = 0;

    for (int i = 0; i < 8 && e->name[i] != ' ' && pos < out_size - 1; i++)
    {
        out[pos++] = (char)e->name[i];
    }

    if (e->ext[0] != ' ' && pos < out_size - 1)
    {
        out[pos++] = '.';
        for (int i = 0; i < 3 && e->ext[i] != ' ' && pos < out_size - 1; i++)
        {
            out[pos++] = (char)e->ext[i];
        }
    }

    out[pos] = '\0';
}

// Returns the index-th real entry in the root directory (0-based, deleted/LFN/volume-label
// entries don't count), preferring its long name when present. No locking around the shared
// LFN buffer here — currently safe only because the shell serializes filesystem-using tasks.
int fat_list_entry(int index, char *name_out, uint32_t name_out_size, uint32_t *size_out)
{
    if (!fat_ready)
    {
        return -1;
    }

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    int entries_per_sector = ATA_SECTOR_SIZE / sizeof(struct fat_dir_entry);
    int seen = 0;

    lfn_reset();

    for (uint32_t s = 0; s < root_dir_sectors; s++)
    {
        if (ata_read_sector(root_dir_start_lba + s, sector_buf) != 0)
        {
            return -1;
        }

        struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;

        for (int i = 0; i < entries_per_sector; i++)
        {
            struct fat_dir_entry *e = &entries[i];

            if (e->name[0] == 0x00)
            {
                return -1; // end of directory — index was out of range
            }
            if (e->name[0] == 0xE5)
            {
                lfn_reset();
                continue;
            }
            if (e->attr == FAT_ATTR_LONG_NAME)
            {
                lfn_accumulate((const struct fat_lfn_entry *)e);
                continue;
            }
            if (e->attr & FAT_ATTR_VOLUME_LABEL)
            {
                lfn_reset();
                continue;
            }

            if (seen == index)
            {
                int use_long_name = lfn_has_data && lfn_checksum_valid &&
                                    fat_short_name_checksum(e) == lfn_checksum;

                if (use_long_name)
                {
                    fat_copy_string(name_out, lfn_buffer, name_out_size);
                }
                else
                {
                    fat_short_name_to_display(e, name_out, name_out_size);
                }

                *size_out = e->file_size;
                lfn_reset();
                return 0;
            }

            seen++;
            lfn_reset();
        }
    }

    return -1;
}