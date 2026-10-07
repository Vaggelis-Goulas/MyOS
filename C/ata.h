#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY 0x1F0
#define ATA_SECONDARY 0x170
#define ATA_REG_DATA 0
#define ATA_REG_ERROR 1
#define ATA_REG_FEATURES 1
#define ATA_REG_SECCOUNT 2
#define ATA_REG_LBA_LO 3
#define ATA_REG_LBA_MID 4
#define ATA_REG_LBA_HI 5
#define ATA_REG_DRIVE 6
#define ATA_REG_STATUS 7
#define ATA_REG_COMMAND 7
#define ATA_REG_ALTSAT 0x3F6
#define ATA_REG_DEVSEL 0x3F6

#define ATA_CMD_READ 0x20
#define ATA_CMD_WRITE 0x30
#define ATA_CMD_IDENTIFY 0xEC

#define ATA_SR_BSY 0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF 0x20
#define ATA_SR_ERR 0x01

typedef struct {
    uint16_t config;
    uint16_t cylinders;
    uint16_t reserved;
    uint16_t heads;
    uint16_t bytes_per_track;
    uint16_t bytes_per_sector;
    uint16_t sector_per_track;
    uint16_t vendor[3];
    uint16_t serial[10];
    uint16_t model[20];
    uint16_t sector_per_unit;
    uint8_t reserved2 [212];
} __attribute__((packed)) ata_identify_t;

void ata_init();
int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, void *buffer);

#endif