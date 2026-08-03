// ATA PIO driver for detecting and reading/writing sectors on the primary master drive

#include <stdint.h>
#include "io.h"
#include "terminal.h"
#include "ata.h"

#define ATA_DATA 0x1F0
#define ATA_ERROR 0x1F1
#define ATA_SECCOUNT 0x1F2
#define ATA_LBA_LOW 0x1F3
#define ATA_LBA_MID 0x1F4
#define ATA_LBA_HIGH 0x1F5
#define ATA_DRIVE_HEAD 0x1F6
#define ATA_STATUS 0x1F7
#define ATA_COMMAND 0x1F7
#define ATA_CONTROL 0x3F6 // alternate status register

#define ATA_CMD_READ_SECTORS 0x20
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_CACHE_FLUSH 0xE7
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_SR_ERR 0x01
#define ATA_SR_DRQ 0x08
#define ATA_SR_BSY 0x80

// Gives the drive ~400ns to settle after a drive-select or command
static void ata_delay_400ns(void)
{
    for (int i = 0; i < 4; i++)
    {
        inb(ATA_CONTROL);
    }
}

// Blocks until the drive clears its busy flag
static void ata_wait_bsy(void)
{
    while (inb(ATA_STATUS) & ATA_SR_BSY)
    {
    }
}

// Blocks until the drive has data ready, or reports an error
static int ata_wait_drq(void)
{
    uint8_t status;
    do
    {
        status = inb(ATA_STATUS);
        if (status & ATA_SR_ERR)
        {
            return -1;
        }
    } while (!(status & ATA_SR_DRQ));

    return 0;
}

// Detects the primary master via IDENTIFY and reports its size
void ata_init(void)
{
    outb(ATA_DRIVE_HEAD, 0xA0); // select master drive
    ata_delay_400ns();

    outb(ATA_SECCOUNT, 0);
    outb(ATA_LBA_LOW, 0);
    outb(ATA_LBA_MID, 0);
    outb(ATA_LBA_HIGH, 0);
    outb(ATA_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_STATUS);
    if (status == 0)
    {
        terminal_writestring("ATA: no drive present on primary master\n");
        return;
    }

    ata_wait_bsy();

    // Non-zero here means this isn't a standard ATA drive
    if (inb(ATA_LBA_MID) != 0 || inb(ATA_LBA_HIGH) != 0)
    {
        terminal_writestring("ATA: primary master is not a standard ATA drive\n");
        return;
    }

    if (ata_wait_drq() != 0)
    {
        terminal_writestring("ATA: IDENTIFY command failed\n");
        return;
    }

    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++)
    {
        identify_data[i] = inw(ATA_DATA);
    }

    // Words 60-61 of the IDENTIFY response hold the 28-bit sector count
    uint32_t total_sectors = ((uint32_t)identify_data[61] << 16) | identify_data[60];

    terminal_writestring("ATA: primary master detected, ");
    terminal_print_dec(total_sectors);
    terminal_writestring(" sectors (");
    terminal_print_dec((total_sectors * ATA_SECTOR_SIZE) / (1024 * 1024));
    terminal_writestring(" MB)\n");
}

int ata_read_sector(uint32_t lba, uint8_t *buffer)
{
    ata_wait_bsy();

    // 0xE0 = LBA mode, master drive; top 4 LBA bits ride in the low nibble
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay_400ns();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_READ_SECTORS);

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
        buf16[i] = inw(ATA_DATA);
    }

    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buffer)
{
    ata_wait_bsy();

    // 0xE0 = LBA mode, master drive; top 4 LBA bits ride in the low nibble
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    ata_delay_400ns();

    outb(ATA_SECCOUNT, 1);
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(ATA_COMMAND, ATA_CMD_WRITE_SECTORS);

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
        outw(ATA_DATA, buf16[i]);
    }

    outb(ATA_COMMAND, ATA_CMD_CACHE_FLUSH); // without this, a write can sit in a volatile cache instead of really landing
    ata_wait_bsy();

    return 0;
}