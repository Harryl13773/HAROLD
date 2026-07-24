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

#define FAT_ATTR_VOLUME_LABEL 0x08
#define FAT_ATTR_LONG_NAME 0x0F

// Layout computed once by fat_init() from the BPB
static uint32_t fat_start_lba;
static uint32_t root_dir_start_lba;
static uint32_t root_dir_sectors;
static uint32_t data_start_lba;
static uint16_t root_entry_count;
static uint8_t sectors_per_cluster;
static int fat_ready = 0;

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

// Scans the root directory for a matching 8.3 name
static int fat_find_entry(const uint8_t *name83, struct fat_dir_entry *out_entry)
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
            struct fat_dir_entry *e = &entries[i];

            if (e->name[0] == 0x00)
            {
                return -1; // end of directory
            }
            if (e->name[0] == 0xE5)
            {
                continue; // deleted entry
            }
            if (e->attr == FAT_ATTR_LONG_NAME)
            {
                continue; // long filename entry, unsupported
            }
            if (e->attr & FAT_ATTR_VOLUME_LABEL)
            {
                continue; // volume label, not a file
            }

            if (fat_dir_entry_matches(e, name83))
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

    uint8_t name83[11];
    fat_name_to_83(filename, name83);

    struct fat_dir_entry entry;
    if (fat_find_entry(name83, &entry) != 0)
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