#include <stdint.h>
#include "ata.h"
#include "terminal.h"
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
static int fat_ready = 0;

// One open file's read position, persisted across calls to fat_read
struct open_file
{
    int in_use;
    uint32_t file_size;
    uint16_t current_cluster;
    uint32_t cluster_offset; // byte offset within current_cluster
    uint32_t file_offset;    // absolute byte offset within the file
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

// Scans the root directory for filename, checking both the reconstructed long name and the 8.3 short name
static int fat_find_entry(const char *filename, struct fat_dir_entry *out_entry)
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
                *out_entry = *e;
                return 0;
            }
        }
    }

    return -1;
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

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        if (!open_files[i].in_use)
        {
            open_files[i].in_use = 1;
            open_files[i].file_size = entry.file_size;
            open_files[i].current_cluster = entry.first_cluster_low;
            open_files[i].cluster_offset = 0;
            open_files[i].file_offset = 0;
            return i + FAT_FD_BASE;
        }
    }

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

// Closes a descriptor opened by fat_open — leaked, not auto-closed, if a task exits without calling this
int fat_close(int fd)
{
    int index = fd - FAT_FD_BASE;
    if (index < 0 || index >= MAX_OPEN_FILES || !open_files[index].in_use)
    {
        return -1;
    }

    open_files[index].in_use = 0;
    return 0;
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

// Returns the index-th real root directory entry, preferring its long name; unsynchronized LFN buffer is safe only because the shell serializes filesystem tasks
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