#ifndef SCSI_DISK_H
#define SCSI_DISK_H

#include "scsi5380.h"

#define SCSI_SECTOR_SIZE 512u
#define SCSI_DISK_MAX_BLOCKS (1u << 21)   /* READ(6)/WRITE(6) have 21-bit LBAs. */

struct scsi_disk_media {
        void *ctx;
        uint32_t blocks;
        /* Return 0 on success. A NULL write callback means write-protected. */
        int (*read)(void *ctx, uint32_t lba, uint8_t sector[SCSI_SECTOR_SIZE]);
        int (*write)(void *ctx, uint32_t lba, const uint8_t sector[SCSI_SECTOR_SIZE]);
        int (*flush)(void *ctx);
};

struct scsi_disk {
        struct scsi_disk_media media;
        uint32_t lba, blocks_left;
        unsigned int data_pos, data_len, command_pos, command_len;
        uint8_t command[16], sector[SCSI_SECTOR_SIZE];
        uint8_t status, sense_key, sense_asc;
        bool clear_sense;
};

/* NULL media represents a present target with no medium. */
void scsi_disk_init(struct scsi_disk *disk, const struct scsi_disk_media *media);
struct scsi_target scsi_disk_target(struct scsi_disk *disk, unsigned int id);

struct scsi_image {
        int fd;
        struct scsi_disk_media media;
};

/* POSIX whole-device image. Keep the image alive at the same address until
 * the disk is detached. Open/close return -1 with errno on failure. */
int scsi_image_open(struct scsi_image *image, const char *path, bool writable);
int scsi_image_close(struct scsi_image *image);

#endif
