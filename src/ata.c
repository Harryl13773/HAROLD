/*
ATA PIO driver for detecting and reading/writing sectors on the master drive of whichever
channel (primary or secondary) actually has one: some hosts, notably UTM/QEMU with a CD-ROM
also attached, put the real disk on the secondary channel instead of the conventional primary.
*/

#include <stdint.h>
#include "io.h"
#include "terminal.h"
#include "ata.h"

// Register offsets from whichever channel's I/O base is currently selected
#define ATA_REG_DATA 0x00
#define ATA_REG_ERROR 0x01
#define ATA_REG_SECCOUNT 0x02
#define ATA_REG_LBA_LOW 0x03
#define ATA_REG_LBA_MID 0x04
#define ATA_REG_LBA_HIGH 0x05
#define ATA_REG_DRIVE_HEAD 0x06
#define ATA_REG_STATUS 0x07
#define ATA_REG_COMMAND 0x07

#define ATA_PRIMARY_IO 0x1F0
#define ATA_PRIMARY_CONTROL 0x3F6
#define ATA_SECONDARY_IO 0x170
#define ATA_SECONDARY_CONTROL 0x376

#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

// Set by ata_init() after finding a drive, so read/write use the correct channel
static uint16_t ata_io_base = ATA_PRIMARY_IO;
static uint16_t ata_control_base = ATA_PRIMARY_CONTROL;

// Gives the drive ~400ns to settle after a drive-select or command
static void ata_delay_400ns(void)
{
    for (int i = 0; i < 4; i++)
    {
        inb(ata_control_base);
    }
}

// Blocks until the drive clears its busy flag
static void ata_wait_bsy(void)
{
    while (inb(ata_io_base + ATA_REG_STATUS) & ATA_SR_BSY)
    {
    }
}

// Blocks until the drive has data ready, or reports an error
static int ata_wait_drq(void)
{
    uint8_t status;
    do
    {
        status = inb(ata_io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR)
        {
            return -1;
        }
    } while (!(status & ATA_SR_DRQ));

    return 0;
}

// Tries IDENTIFY on the master drive; on success, reports its size and selects this channel for I/O
static int ata_try_channel(uint16_t io_base, uint16_t control_base, const char *label)
{
    ata_io_base = io_base;
    ata_control_base = control_base;

    outb(ata_io_base + ATA_REG_DRIVE_HEAD, 0xA0); // select master drive
    ata_delay_400ns();

    outb(ata_io_base + ATA_REG_SECCOUNT, 0);
    outb(ata_io_base + ATA_REG_LBA_LOW, 0);
    outb(ata_io_base + ATA_REG_LBA_MID, 0);
    outb(ata_io_base + ATA_REG_LBA_HIGH, 0);
    outb(ata_io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ata_io_base + ATA_REG_STATUS);
    if (status == 0)
    {
        terminal_writestring("ATA: no drive present on ");
        terminal_writestring(label);
        terminal_writestring(" master\n");
        return -1;
    }

    ata_wait_bsy();

    // Non-zero here means this isn't a standard ATA drive
    if (inb(ata_io_base + ATA_REG_LBA_MID) != 0 || inb(ata_io_base + ATA_REG_LBA_HIGH) != 0)
    {
        terminal_writestring("ATA: ");
        terminal_writestring(label);
        terminal_writestring(" master is not a standard ATA drive\n");
        return -1;
    }

    if (ata_wait_drq() != 0)
    {
        terminal_writestring("ATA: IDENTIFY command failed on ");
        terminal_writestring(label);
        terminal_writestring("\n");
        return -1;
    }

    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++)
    {
        identify_data[i] = inw(ata_io_base + ATA_REG_DATA);
    }

    // Words 60-61 of the IDENTIFY response hold the 28-bit sector count
    uint32_t total_sectors = ((uint32_t)identify_data[61] << 16) | identify_data[60];

    terminal_writestring("ATA: ");
    terminal_writestring(label);
    terminal_writestring(" master detected, ");
    terminal_print_dec(total_sectors);
    terminal_writestring(" sectors (");
    terminal_print_dec((total_sectors * ATA_SECTOR_SIZE) / (1024 * 1024));
    terminal_writestring(" MB)\n");
    return 0;
}

// Detects the master drive, trying primary first, then secondary if needed
void ata_init(void)
{
    if (ata_try_channel(ATA_PRIMARY_IO, ATA_PRIMARY_CONTROL, "primary") == 0)
    {
        return;
    }
    ata_try_channel(ATA_SECONDARY_IO, ATA_SECONDARY_CONTROL, "secondary");
}

// Reads one 512-byte sector at the given LBA into buffer
int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    ata_wait_bsy();

    // 0xE0 = LBA mode, master drive; top 4 LBA bits ride in the low nibble
    outb(ata_io_base + ATA_REG_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay_400ns();

    outb(ata_io_base + ATA_REG_SECCOUNT, 1);
    outb(ata_io_base + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ata_io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata_io_base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ata_io_base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);

    ata_wait_bsy();

    if (ata_wait_drq() != 0)
    {
        terminal_writestring("ATA: read error at LBA ");
        terminal_print_dec(lba);
        terminal_writestring("\n");
        return -1;
    }

    // Data register is 16 bits wide, so a 512-byte sector is 256 words
    uint16_t *buf16 = (uint16_t *)buffer;
    for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++)
    {
        buf16[i] = inw(ata_io_base + ATA_REG_DATA);
    }

    return 0;
}

// Writes one 512-byte sector at the given LBA from buffer, flushing the drive's cache afterward
int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    ata_wait_bsy();

    // 0xE0 = LBA mode, master drive; top 4 LBA bits ride in the low nibble
    outb(ata_io_base + ATA_REG_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay_400ns();

    outb(ata_io_base + ATA_REG_SECCOUNT, 1);
    outb(ata_io_base + ATA_REG_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ata_io_base + ATA_REG_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ata_io_base + ATA_REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ata_io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);

    ata_wait_bsy();

    if (ata_wait_drq() != 0)
    {
        terminal_writestring("ATA: write error at LBA ");
        terminal_print_dec(lba);
        terminal_writestring("\n");
        return -1;
    }

    const uint16_t *buf16 = (const uint16_t *)buffer;
    for (int i = 0; i < ATA_SECTOR_SIZE / 2; i++)
    {
        outw(ata_io_base + ATA_REG_DATA, buf16[i]);
    }

    outb(ata_io_base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH); // without this, a write can sit in a volatile cache instead of really landing
    ata_wait_bsy();

    return 0;
}
