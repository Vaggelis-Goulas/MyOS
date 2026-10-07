#include "ata.h"
#include "io.h"
#include "vga.h"
#include "task.h"

static uint16_t ata_base = ATA_PRIMARY;
static int ata_drive = 0;
static int ata_present = 0;

static void ata_wait_ready() {
    while (inb(ata_base + ATA_REG_STATUS) & ATA_SR_BSY);
}

void ata_init() {
    outb(ata_base + ATA_REG_DRIVE, 0xE0 | (ata_drive << 4));
    
    for (volatile int i = 0; i < 1000; i++) {
        asm volatile("pause");
    }
    
    uint8_t status = inb(ata_base + ATA_REG_STATUS);
    if (status == 0xFF || status == 0x00) {
        vga_print("[ATA] No drive found.\n");
        ata_present = 0;
        return;
    }
    
    vga_print("[ATA] Drive detected.\n");
    ata_present = 1;
}

int ata_read_sectors(uint32_t lba, uint8_t count, void *buffer) {
    preempt_disable();
    if (!ata_present) { preempt_enable(); return -1; }
    ata_wait_ready();
    outb(ata_base + ATA_REG_DRIVE, 0xE0 | (ata_drive << 4) | ((lba >> 24) & 0x0F));
    outb(ata_base + ATA_REG_SECCOUNT, count);
    outb(ata_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ata_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_READ);

    uint16_t *buf = (uint16_t*)buffer;
    for (int s = 0; s < count; s++) {
        ata_wait_ready();
        uint8_t status = inb(ata_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) { preempt_enable(); return -1; }
        for (int i = 0; i < 256; i++) {
            buf[i + s * 256] = inw(ata_base + ATA_REG_DATA);
        }
    }
    preempt_enable();
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t count, void *buffer) {
    preempt_disable();
    if (!ata_present) { preempt_enable(); return -1; }
    ata_wait_ready();
    outb(ata_base + ATA_REG_DRIVE, 0xE0 | (ata_drive << 4) | ((lba >> 24) & 0x0F));
    outb(ata_base + ATA_REG_SECCOUNT, count);
    outb(ata_base + ATA_REG_LBA_LO, lba & 0xFF);
    outb(ata_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(ata_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
    outb(ata_base + ATA_REG_COMMAND, ATA_CMD_WRITE);

    uint16_t *buf = (uint16_t*)buffer;
    for (int s = 0; s < count; s++) {
        ata_wait_ready();
        for (int i = 0; i < 256; i++) {
            outw(ata_base + ATA_REG_DATA, buf[i + s * 256]);
        }
    }
    ata_wait_ready();
    preempt_enable();
    return 0;
}