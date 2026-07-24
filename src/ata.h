#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_SECTOR_SIZE 512

// Detects the primary master drive and reports its size
void ata_init(void);

// Reads one 512-byte sector at the given LBA into buffer
int ata_read_sector(uint32_t lba, uint8_t *buffer);

#endif