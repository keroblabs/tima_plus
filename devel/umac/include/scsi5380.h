#ifndef SCSI5380_H
#define SCSI5380_H

#include <stdbool.h>
#include <stdint.h>

/* Information transfer phases use the NCR target-command register encoding. */
enum scsi_phase {
        SCSI_DATA_OUT = 0,
        SCSI_DATA_IN = 1,
        SCSI_COMMAND = 2,
        SCSI_STATUS = 3,
        SCSI_MESSAGE_IN = 7,
        SCSI_BUS_FREE,
        SCSI_SELECTION,
};

struct scsi_target {
        unsigned int id;                 /* 0-6; the Mac is ID 7 */
        void *ctx;
        /* Discard an unfinished transfer. Only SCSI RST resets target sense. */
        void (*reset)(void *ctx, bool bus_reset);
        uint8_t (*peek)(void *ctx, enum scsi_phase phase);
        /* Called once per completed byte. Return the next phase. */
        enum scsi_phase (*transfer)(void *ctx, enum scsi_phase phase, uint8_t data);
};

/* Copies the descriptor, borrowing its context until detached (NULL). */
void ncr5380_attach(const struct scsi_target *target);
void ncr5380_reset(void);
uint8_t ncr5380_read_register(unsigned int reg);
void ncr5380_write_register(unsigned int reg, uint8_t data);
uint8_t ncr5380_pseudo_dma_read(void);
void ncr5380_pseudo_dma_write(uint8_t data);

#endif
