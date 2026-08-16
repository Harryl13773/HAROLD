// FAT16 filesystem driver: boot sector parsing, directory/LFN traversal, path resolution across
// subdirectories, and file read/write (including in-place, via seek).

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
#define FAT_ATTR_DIRECTORY 0x10
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
    uint16_t first_cluster;  // this file's very first cluster — lets fat_seek restart from the start
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

// Reads the boot sector on the primary master, validates it's FAT16, and computes the volume layout
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

// Identifies a directory as either the fixed FAT16 root or a subdirectory cluster chain
struct fat_dir_ref
{
    int is_root;
    uint16_t first_cluster;
};

// Builds a directory reference pointing at the root
static struct fat_dir_ref fat_root_dir_ref(void)
{
    struct fat_dir_ref dir = {1, 0};
    return dir;
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

// Returns the LBA of dir's n-th sector, or 0 past the end
static uint32_t fat_dir_sector_lba(struct fat_dir_ref dir, uint32_t n)
{
    if (dir.is_root)
    {
        if (n >= root_dir_sectors)
        {
            return 0;
        }
        return root_dir_start_lba + n;
    }

    uint16_t cluster = dir.first_cluster;
    uint32_t sector_in_chain = n;

    while (sector_in_chain >= sectors_per_cluster)
    {
        if (cluster < 2 || cluster >= 0xFFF8)
        {
            return 0;
        }
        cluster = fat_get_next_cluster(cluster);
        sector_in_chain -= sectors_per_cluster;
    }

    if (cluster < 2 || cluster >= 0xFFF8)
    {
        return 0;
    }

    return data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster + sector_in_chain;
}

// Finds an entry by long or 8.3 name and returns its disk location
static int fat_find_entry_in_dir(struct fat_dir_ref dir, const char *component, uint32_t *out_sector, uint32_t *out_offset, struct fat_dir_entry *out_entry)
{
    uint8_t name83[11];
    fat_name_to_83(component, name83);

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    int entries_per_sector = ATA_SECTOR_SIZE / sizeof(struct fat_dir_entry);

    lfn_reset();

    for (uint32_t s = 0;; s++)
    {
        uint32_t lba = fat_dir_sector_lba(dir, s);
        if (lba == 0)
        {
            return -1; // past the end of this directory's currently allocated space
        }

        if (ata_read_sector(lba, sector_buf) != 0)
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
                matched = names_equal_ci(component, lfn_buffer);
            }

            lfn_reset();

            if (matched)
            {
                *out_sector = lba;
                *out_offset = (uint32_t)i;
                *out_entry = *e;
                return 0;
            }
        }
    }
}

#define FAT_MAX_COMPONENT 48 // generous — 8.3 short names cap at 11 chars, longest real filename here is 14

// Extracts the next '/' separated path component, returning its length or 0 if empty.
static uint32_t fat_next_path_component(const char **path_ptr, char *out, uint32_t out_size)
{
    const char *p = *path_ptr;
    uint32_t len = 0;

    while (p[len] != '\0' && p[len] != '/')
    {
        len++;
    }

    uint32_t copy_len = (len < out_size - 1) ? len : out_size - 1;
    for (uint32_t i = 0; i < copy_len; i++)
    {
        out[i] = p[i];
    }
    out[copy_len] = '\0';

    *path_ptr = (p[len] == '/') ? p + len + 1 : p + len;
    return len;
}

// Resolves a path's parent directory and returns its final component name
static int fat_resolve_parent(const char *path, struct fat_dir_ref *out_parent, char *out_name, uint32_t out_name_size)
{
    struct fat_dir_ref current = fat_root_dir_ref();
    const char *p = path;

    while (1)
    {
        char component[FAT_MAX_COMPONENT];
        if (fat_next_path_component(&p, component, sizeof(component)) == 0)
        {
            return -1; // empty component — "", "a//b", or similar
        }

        if (*p == '\0')
        {
            // this was the last component — hand it back unresolved, caller looks it up itself
            fat_copy_string(out_name, component, out_name_size);
            *out_parent = current;
            return 0;
        }

        // not the last component — must already exist and be a directory to keep descending
        uint32_t dsector, doffset;
        struct fat_dir_entry dentry;
        if (fat_find_entry_in_dir(current, component, &dsector, &doffset, &dentry) != 0)
        {
            return -1;
        }
        if (!(dentry.attr & FAT_ATTR_DIRECTORY))
        {
            return -1; // a path component that isn't actually a directory
        }

        current.is_root = 0;
        current.first_cluster = dentry.first_cluster_low;
    }
}

// Resolves a directory path, with an empty path referring to the root
static int fat_resolve_dir(const char *dir_path, struct fat_dir_ref *out_dir)
{
    struct fat_dir_ref current = fat_root_dir_ref();
    const char *p = dir_path;

    while (*p != '\0')
    {
        char component[FAT_MAX_COMPONENT];
        if (fat_next_path_component(&p, component, sizeof(component)) == 0)
        {
            return -1;
        }

        uint32_t dsector, doffset;
        struct fat_dir_entry dentry;
        if (fat_find_entry_in_dir(current, component, &dsector, &doffset, &dentry) != 0)
        {
            return -1;
        }
        if (!(dentry.attr & FAT_ATTR_DIRECTORY))
        {
            return -1;
        }

        current.is_root = 0;
        current.first_cluster = dentry.first_cluster_low;
    }

    *out_dir = current;
    return 0;
}

// Resolves a path and returns the final entry's disk location
static int fat_find_entry_location(const char *path, uint32_t *out_sector, uint32_t *out_offset, struct fat_dir_entry *out_entry)
{
    struct fat_dir_ref parent;
    char name[FAT_MAX_COMPONENT];
    if (fat_resolve_parent(path, &parent, name, sizeof(name)) != 0)
    {
        return -1;
    }
    return fat_find_entry_in_dir(parent, name, out_sector, out_offset, out_entry);
}

// Convenience wrapper for callers that only need an entry's contents, not where it lives on disk
static int fat_find_entry(const char *filename, struct fat_dir_entry *out_entry)
{
    uint32_t sector, offset;
    return fat_find_entry_location(filename, &sector, &offset, out_entry);
}

// Looks up filename and copies its whole contents into buffer in one call
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

    if (entry.attr & FAT_ATTR_DIRECTORY)
    {
        terminal_writestring("FAT: is a directory, not a file: ");
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

    if (entry.attr & FAT_ATTR_DIRECTORY)
    {
        return -1; // not a file
    }

    // Make descriptor allocation atomic to prevent task switches from corrupting the shared table
    uint32_t flags = save_and_disable_interrupts();

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!open_files[i].in_use)
        {
            open_files[i].in_use = 1;
            open_files[i].file_size = entry.file_size;
            open_files[i].current_cluster = entry.first_cluster_low;
            open_files[i].first_cluster = entry.first_cluster_low;
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

// Repositions fd to an absolute offset, walking the cluster chain from the start
int fat_seek(int fd, uint32_t offset)
{
    int index = fd - FAT_FD_BASE;
    if (index < 0 || index >= MAX_OPEN_FILES || !open_files[index].in_use)
    {
        return -1;
    }

    struct open_file *f = &open_files[index];
    if (offset > f->file_size)
    {
        return -1; // no sparse files — can't seek past the current end
    }

    uint32_t cluster_size = (uint32_t)sectors_per_cluster * ATA_SECTOR_SIZE;
    uint16_t cluster = f->first_cluster;
    uint32_t remaining = offset;

    while (remaining >= cluster_size)
    {
        uint16_t next = fat_get_next_cluster(cluster);
        if (next < 2 || next >= 0xFFF8)
        {

            // At chain end, mark the last cluster full so fat_write allocates a new one.
            remaining = cluster_size;
            break;
        }
        cluster = next;
        remaining -= cluster_size;
    }

    f->current_cluster = cluster; // 0 only for an empty file, triggering fat_write's lazy allocation.
    f->cluster_offset = remaining;
    f->file_offset = offset;
    return 0;
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

// Closes all descriptors owned by a task to prevent leaks on exit.
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

// Allocates the first free cluster, or returns 0 if the disk is full.
static uint16_t fat_alloc_cluster(void)
{
    uint32_t total_entries = fat_size_sectors * (ATA_SECTOR_SIZE / 2);
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    uint32_t loaded_sector = 0xFFFFFFFF; // sentinel: no sector loaded yet, never a real sector number

    for (uint32_t cluster = 2; cluster < total_entries; cluster++)
    {
        uint32_t fat_offset = cluster * 2;
        uint32_t sector = fat_start_lba + (fat_offset / ATA_SECTOR_SIZE);
        uint32_t offset_in_sector = fat_offset % ATA_SECTOR_SIZE;

        // Reload the FAT sector when needed to avoid scanning stale or uninitialized data
        if (sector != loaded_sector)
        {
            if (ata_read_sector(sector, sector_buf) != 0)
            {
                return 0;
            }
            loaded_sector = sector;
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

// Finds a free directory slot, extending full subdirectories as needed
static int fat_alloc_dir_entry_in_dir(struct fat_dir_ref dir, uint32_t *out_sector, uint32_t *out_offset)
{
    uint8_t sector_buf[ATA_SECTOR_SIZE];
    int entries_per_sector = ATA_SECTOR_SIZE / sizeof(struct fat_dir_entry);

    for (uint32_t s = 0;; s++)
    {
        uint32_t lba = fat_dir_sector_lba(dir, s);
        if (lba == 0)
        {
            break; // ran out of this directory's currently allocated space
        }

        if (ata_read_sector(lba, sector_buf) != 0)
        {
            return -1;
        }

        struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;

        for (int i = 0; i < entries_per_sector; i++)
        {
            if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5)
            {
                *out_sector = lba;
                *out_offset = (uint32_t)i;
                return 0;
            }
        }
    }

    if (dir.is_root)
    {
        return -1; // root directory full — FAT16's root dir has a fixed size, unlike subdirectories
    }

    uint16_t last = fat_find_last_cluster(dir.first_cluster);
    if (last == 0)
    {
        return -1; // shouldn't happen — every subdirectory this driver creates starts with 1 cluster
    }

    uint16_t new_cluster = fat_alloc_cluster();
    if (new_cluster == 0)
    {
        return -1; // disk full
    }

    uint8_t zero_buf[ATA_SECTOR_SIZE];
    for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
    {
        zero_buf[i] = 0;
    }

    uint32_t new_cluster_lba = data_start_lba + (uint32_t)(new_cluster - 2) * sectors_per_cluster;
    for (uint32_t s = 0; s < sectors_per_cluster; s++)
    {
        ata_write_sector(new_cluster_lba + s, zero_buf); // all-zero reads as "empty", same as root's own initial state
    }

    fat_set_cluster_entry(last, new_cluster); // link the new cluster onto the chain

    *out_sector = new_cluster_lba;
    *out_offset = 0;
    return 0;
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

// Creates an empty subdirectory; the parent must exist and the name must be unused
int fat_mkdir(const char *path)
{
    if (!fat_ready)
    {
        return -1;
    }

    struct fat_dir_ref parent;
    char name[FAT_MAX_COMPONENT];
    if (fat_resolve_parent(path, &parent, name, sizeof(name)) != 0)
    {
        return -1;
    }

    uint32_t existing_sector, existing_offset;
    struct fat_dir_entry existing;
    if (fat_find_entry_in_dir(parent, name, &existing_sector, &existing_offset, &existing) == 0)
    {
        return -1; // name already in use
    }

    uint16_t new_cluster = fat_alloc_cluster();
    if (new_cluster == 0)
    {
        return -1; // disk full
    }

    // Initialize the directory cluster with standard "." and ".." entries
    uint8_t buf[ATA_SECTOR_SIZE];
    uint32_t new_cluster_lba = data_start_lba + (uint32_t)(new_cluster - 2) * sectors_per_cluster;

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
    {
        buf[i] = 0;
    }

    struct fat_dir_entry *entries = (struct fat_dir_entry *)buf;

    entries[0].name[0] = '.';
    for (int i = 1; i < 8; i++)
    {
        entries[0].name[i] = ' ';
    }
    for (int i = 0; i < 3; i++)
    {
        entries[0].ext[i] = ' ';
    }
    entries[0].attr = FAT_ATTR_DIRECTORY;
    entries[0].first_cluster_low = new_cluster;
    entries[0].file_size = 0;

    entries[1].name[0] = '.';
    entries[1].name[1] = '.';
    for (int i = 2; i < 8; i++)
    {
        entries[1].name[i] = ' ';
    }
    for (int i = 0; i < 3; i++)
    {
        entries[1].ext[i] = ' ';
    }
    entries[1].attr = FAT_ATTR_DIRECTORY;
    entries[1].first_cluster_low = parent.is_root ? 0 : parent.first_cluster; // 0 = "parent is root", standard FAT convention
    entries[1].file_size = 0;

    if (ata_write_sector(new_cluster_lba, buf) != 0)
    {
        fat_set_cluster_entry(new_cluster, 0x0000);
        return -1;
    }

    for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++)
    {
        buf[i] = 0;
    }
    for (uint32_t s = 1; s < sectors_per_cluster; s++)
    {
        ata_write_sector(new_cluster_lba + s, buf);
    }

    uint32_t dir_sector, dir_offset;
    if (fat_alloc_dir_entry_in_dir(parent, &dir_sector, &dir_offset) != 0)
    {
        fat_set_cluster_entry(new_cluster, 0x0000); // don't leak the cluster if the parent has no room
        return -1;
    }

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    if (ata_read_sector(dir_sector, sector_buf) != 0)
    {
        fat_set_cluster_entry(new_cluster, 0x0000);
        return -1;
    }

    struct fat_dir_entry *e = &((struct fat_dir_entry *)sector_buf)[dir_offset];
    uint8_t name83[11];
    fat_name_to_83(name, name83);
    for (int i = 0; i < 8; i++)
    {
        e->name[i] = name83[i];
    }
    for (int i = 0; i < 3; i++)
    {
        e->ext[i] = name83[8 + i];
    }
    e->attr = FAT_ATTR_DIRECTORY;
    e->reserved = 0;
    e->create_time_tenth = 0;
    e->create_time = 0;
    e->create_date = 0;
    e->last_access_date = 0;
    e->first_cluster_high = 0;
    e->write_time = 0;
    e->write_date = 0;
    e->first_cluster_low = new_cluster;
    e->file_size = 0;

    if (ata_write_sector(dir_sector, sector_buf) != 0)
    {
        fat_set_cluster_entry(new_cluster, 0x0000);
        return -1;
    }

    return 0;
}

// Creates or opens a file for writing, returning its fd or -1
int fat_open_write(const char *filename, int mode)
{
    if (!fat_ready)
    {
        return -1;
    }

    struct fat_dir_ref parent;
    char name[FAT_MAX_COMPONENT];
    if (fat_resolve_parent(filename, &parent, name, sizeof(name)) != 0)
    {
        return -1; // an intermediate path component doesn't exist or isn't a directory
    }

    uint32_t dir_sector, dir_offset;
    struct fat_dir_entry existing;
    int exists = (fat_find_entry_in_dir(parent, name, &dir_sector, &dir_offset, &existing) == 0);

    if (exists && (existing.attr & FAT_ATTR_DIRECTORY))
    {
        return -1; // can't open a directory for writing
    }

    if (exists && mode == FAT_OPEN_CREATE)
    {
        return -1; // strict create — caller must ask for TRUNCATE, APPEND, or MODIFY to touch an existing file
    }
    if (!exists && fat_alloc_dir_entry_in_dir(parent, &dir_sector, &dir_offset) != 0)
    {
        return -1; // parent directory full (or, for root, genuinely full)
    }

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    if (ata_read_sector(dir_sector, sector_buf) != 0)
    {
        return -1;
    }
    struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;
    struct fat_dir_entry *e = &entries[dir_offset];

    uint16_t start_cluster = 0;
    uint16_t file_first_cluster = 0;
    uint32_t start_file_size = 0;
    uint32_t start_file_offset = 0;
    uint32_t start_cluster_offset = 0;

    if (!exists) // a brand new zero-length entry, same as the old create-only behavior
    {
        uint8_t name83[11];
        fat_name_to_83(name, name83);
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
    else if (mode == FAT_OPEN_APPEND) // resume writing from wherever the file currently ends
    {
        start_file_size = existing.file_size;
        start_file_offset = start_file_size;
        file_first_cluster = existing.first_cluster_low;

        if (existing.first_cluster_low != 0)
        {
            start_cluster = fat_find_last_cluster(existing.first_cluster_low);

            uint32_t cluster_size = (uint32_t)sectors_per_cluster * ATA_SECTOR_SIZE;
            start_cluster_offset = start_file_size % cluster_size;
            if (start_cluster_offset == 0 && start_file_size > 0)
            {
                // If the last cluster is full, force fat_write to allocate a new one
                start_cluster_offset = cluster_size;
            }
        }
    }
    else // FAT_OPEN_MODIFY preserves content and starts at the beginning for in-place edits
    {
        start_file_size = existing.file_size;
        start_file_offset = 0;
        file_first_cluster = existing.first_cluster_low;
        start_cluster = existing.first_cluster_low; // 0 (empty file) matches fat_write's lazy-alloc check
        start_cluster_offset = 0;
    }

    uint32_t flags = save_and_disable_interrupts(); // same shared-table race as fat_open — see the comment there

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!open_files[i].in_use)
        {
            open_files[i].in_use = 1;
            open_files[i].file_size = start_file_size;
            open_files[i].current_cluster = start_cluster; // 0 means "allocate lazily on first write"
            open_files[i].first_cluster = file_first_cluster;
            open_files[i].cluster_offset = start_cluster_offset;
            open_files[i].file_offset = start_file_offset;
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

            if (f->first_cluster == 0) // this file had nothing allocated at all until just now
            {
                f->first_cluster = new_cluster;
            }

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

    // Persist the updated size immediately so it remains correct even without fat_close
    if (ata_read_sector(f->dir_entry_sector, sector_buf) == 0)
    {
        struct fat_dir_entry *entries = (struct fat_dir_entry *)sector_buf;
        entries[f->dir_entry_offset].file_size = f->file_size;
        ata_write_sector(f->dir_entry_sector, sector_buf);
    }

    return (int)bytes_written;
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

// Returns the index-th visible directory entry, preferring its long name when available
int fat_list_entry(const char *dir_path, int index, char *name_out, uint32_t name_out_size, uint32_t *size_out, int *is_dir_out)
{
    if (!fat_ready)
    {
        return -1;
    }

    struct fat_dir_ref dir;
    if (fat_resolve_dir(dir_path, &dir) != 0)
    {
        return -1;
    }

    uint8_t sector_buf[ATA_SECTOR_SIZE];
    int entries_per_sector = ATA_SECTOR_SIZE / sizeof(struct fat_dir_entry);
    int seen = 0;

    lfn_reset();

    for (uint32_t s = 0;; s++)
    {
        uint32_t lba = fat_dir_sector_lba(dir, s);
        if (lba == 0)
        {
            return -1; // end of this directory's currently allocated space — index was out of range
        }

        if (ata_read_sector(lba, sector_buf) != 0)
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

            // Skip "." and ".." since they aren't useful in directory listings
            if (e->name[0] == '.' && (e->name[1] == ' ' || (e->name[1] == '.' && e->name[2] == ' ')))
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
                *is_dir_out = (e->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
                lfn_reset();
                return 0;
            }

            seen++;
            lfn_reset();
        }
    }
}
