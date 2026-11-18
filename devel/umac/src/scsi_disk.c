#include <string.h>

#include "scsi_disk.h"

enum {
        TEST_UNIT_READY = 0x00,
        REQUEST_SENSE = 0x03,
        READ_6 = 0x08,
        WRITE_6 = 0x0a,
        INQUIRY = 0x12,
        MODE_SENSE_6 = 0x1a,
        READ_10 = 0x28,
        WRITE_10 = 0x2a,

        GOOD = 0x00,
        CHECK_CONDITION = 0x02,
        NOT_READY = 0x02,
        MEDIUM_ERROR = 0x03,
        ILLEGAL_REQUEST = 0x05,
        DATA_PROTECT = 0x07,
};

static void disk_reset(void *ctx, bool bus_reset)
{
        struct scsi_disk *disk = ctx;
        disk->command_pos = disk->command_len = 0;
        disk->data_pos = disk->data_len = disk->blocks_left = 0;
        disk->status = GOOD;
        disk->clear_sense = false;
        if (bus_reset)
                disk->sense_key = disk->sense_asc = 0;
}

static enum scsi_phase check_condition(struct scsi_disk *disk, uint8_t key, uint8_t asc)
{
        disk->sense_key = key;
        disk->sense_asc = asc;
        disk->status = CHECK_CONDITION;
        return SCSI_STATUS;
}

static enum scsi_phase reply(struct scsi_disk *disk, unsigned int length)
{
        unsigned int allocation = disk->command[4];
        disk->data_len = allocation < length ? allocation : length;
        return disk->data_len ? SCSI_DATA_IN : SCSI_STATUS;
}

static enum scsi_phase read_sector(struct scsi_disk *disk)
{
        if (disk->media.read(disk->media.ctx, disk->lba, disk->sector))
                return check_condition(disk, MEDIUM_ERROR, 0x11);     /* Unrecovered read error */
        return SCSI_DATA_IN;
}

static void store_be24(uint8_t *dest, uint32_t value)
{
        dest[0] = value >> 16;
        dest[1] = value >> 8;
        dest[2] = value;
}

static enum scsi_phase execute_command(struct scsi_disk *disk)
{
        const uint8_t *cmd = disk->command;
        unsigned int lun = cmd[1] >> 5;
        bool rw10 = cmd[0] == READ_10 || cmd[0] == WRITE_10;

        if (cmd[0] != REQUEST_SENSE)
                disk->sense_key = disk->sense_asc = 0;

        switch (cmd[0]) {
        case TEST_UNIT_READY:
        case REQUEST_SENSE:
        case READ_6:
        case READ_10:
        case WRITE_6:
        case WRITE_10:
        case INQUIRY:
        case MODE_SENSE_6:
                break;
        default:
                return check_condition(disk, ILLEGAL_REQUEST, 0x20);  /* Invalid opcode */
        }

        if (lun && cmd[0] != INQUIRY)
                return check_condition(disk, ILLEGAL_REQUEST, 0x25);  /* Unsupported LUN */
        if (cmd[rw10 ? 9 : 5])
                return check_condition(disk, ILLEGAL_REQUEST, 0x24);  /* No linked commands */

        switch (cmd[0]) {
        case INQUIRY:
                if ((cmd[1] & 0x1f) || cmd[2] || cmd[3])
                        return check_condition(disk, ILLEGAL_REQUEST, 0x24); /* No VPD pages */
                memset(disk->sector, 0, 36);
                disk->sector[0] = lun ? 0x7f : 0x00;
                disk->sector[2] = 2;                     /* SCSI-2 INQUIRY/sense format */
                disk->sector[3] = 2;
                disk->sector[4] = 31;
                memcpy(&disk->sector[8], "uMac    SCSI disk       1.0 ", 28);
                return reply(disk, 36);

        case REQUEST_SENSE:
                if ((cmd[1] & 0x1f) || cmd[2] || cmd[3])
                        return check_condition(disk, ILLEGAL_REQUEST, 0x24);
                memset(disk->sector, 0, 18);
                disk->sector[0] = 0x70;
                disk->sector[2] = disk->sense_key;
                disk->sector[7] = 10;
                disk->sector[12] = disk->sense_asc;
                disk->clear_sense = true;
                return reply(disk, 18);

        default:
                break;
        }

        if (!disk->media.blocks || !disk->media.read)
                return check_condition(disk, NOT_READY, 0x3a);        /* Medium not present */

        if (cmd[0] == TEST_UNIT_READY) {
                if (cmd[1] || cmd[2] || cmd[3] || cmd[4])
                        return check_condition(disk, ILLEGAL_REQUEST, 0x24);
                return SCSI_STATUS;
        }

        if (cmd[0] == MODE_SENSE_6) {
                /* Current header/block descriptor only, no mode pages. */
                if ((cmd[1] & ~0x08) || (cmd[2] != 0 && cmd[2] != 0x3f) || cmd[3])
                        return check_condition(disk, ILLEGAL_REQUEST, 0x24);
                unsigned int length = (cmd[1] & 0x08) ? 4 : 12;
                memset(disk->sector, 0, length);
                disk->sector[0] = length - 1;
                disk->sector[2] = disk->media.write ? 0 : 0x80;
                if (length == 12) {
                        disk->sector[3] = 8;
                        store_be24(&disk->sector[5], disk->media.blocks);
                        store_be24(&disk->sector[9], SCSI_SECTOR_SIZE);
                }
                return reply(disk, length);
        }

        if (rw10) {
                if ((cmd[1] & 0x1f) || cmd[6])
                        return check_condition(disk, ILLEGAL_REQUEST, 0x24);
                disk->lba = ((uint32_t)cmd[2] << 24) | ((uint32_t)cmd[3] << 16) |
                            (cmd[4] << 8) | cmd[5];
                disk->blocks_left = (cmd[7] << 8) | cmd[8];
                if (!disk->blocks_left)
                        return SCSI_STATUS;
        } else {
                disk->lba = ((uint32_t)(cmd[1] & 0x1f) << 16) | (cmd[2] << 8) | cmd[3];
                disk->blocks_left = cmd[4] ? cmd[4] : 256;
        }
        if (disk->lba >= disk->media.blocks || disk->blocks_left > disk->media.blocks - disk->lba)
                return check_condition(disk, ILLEGAL_REQUEST, 0x21);  /* LBA out of range */
        disk->data_len = SCSI_SECTOR_SIZE;
        if (cmd[0] != WRITE_6 && cmd[0] != WRITE_10)
                return read_sector(disk);
        if (!disk->media.write)
                return check_condition(disk, DATA_PROTECT, 0x27);     /* Write protected */
        return SCSI_DATA_OUT;
}

static uint8_t disk_peek(void *ctx, enum scsi_phase phase)
{
        struct scsi_disk *disk = ctx;
        if (phase == SCSI_STATUS)
                return disk->status;
        if (phase == SCSI_DATA_IN)
                return disk->sector[disk->data_pos];
        return 0;                       /* COMMAND COMPLETE in MESSAGE IN */
}

static enum scsi_phase disk_transfer(void *ctx, enum scsi_phase phase, uint8_t data)
{
        struct scsi_disk *disk = ctx;
        switch (phase) {
        case SCSI_COMMAND:
                if (disk->command_pos == 0) {
                        static const uint8_t lengths[] = {6, 10, 10, 0, 16, 12, 0, 0};
                        disk->command_len = lengths[data >> 5];
                        if (!disk->command_len)
                                return check_condition(disk, ILLEGAL_REQUEST, 0x20);
                }
                disk->command[disk->command_pos++] = data;
                return disk->command_pos == disk->command_len ? execute_command(disk) : phase;

        case SCSI_DATA_IN:
        case SCSI_DATA_OUT:
                if (phase == SCSI_DATA_OUT)
                        disk->sector[disk->data_pos] = data;
                if (++disk->data_pos < disk->data_len)
                        return phase;
                disk->data_pos = 0;

                if (phase == SCSI_DATA_IN) {
                        if (!disk->blocks_left || !--disk->blocks_left)
                                return SCSI_STATUS;
                        ++disk->lba;
                        return read_sector(disk);
                }
                if (disk->media.write(disk->media.ctx, disk->lba, disk->sector))
                        return check_condition(disk, MEDIUM_ERROR, 0x0c); /* Write error */
                if (--disk->blocks_left) {
                        ++disk->lba;
                        return phase;
                }
                if (disk->media.flush && disk->media.flush(disk->media.ctx))
                        return check_condition(disk, MEDIUM_ERROR, 0x0c);
                return SCSI_STATUS;

        case SCSI_STATUS:
                return SCSI_MESSAGE_IN;
        case SCSI_MESSAGE_IN:
                if (disk->clear_sense)
                        disk->sense_key = disk->sense_asc = 0;
                return SCSI_BUS_FREE;
        default:
                return SCSI_BUS_FREE;
        }
}

void scsi_disk_init(struct scsi_disk *disk, const struct scsi_disk_media *media)
{
        *disk = (struct scsi_disk){0};
        if (media)
                disk->media = *media;
}

struct scsi_target scsi_disk_target(struct scsi_disk *disk, unsigned int id)
{
        return (struct scsi_target) {
                .id = id,
                .ctx = disk,
                .reset = disk_reset,
                .peek = disk_peek,
                .transfer = disk_transfer,
        };
}
