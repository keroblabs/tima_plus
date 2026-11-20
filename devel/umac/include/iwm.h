#ifndef IWM_H
#define IWM_H

#include <stdbool.h>
#include <stdint.h>

struct disk_img;

enum {
        IWM_PHASE0 = 1 << 0,
        IWM_PHASE1 = 1 << 1,
        IWM_PHASE2 = 1 << 2,
        IWM_STROBE = 1 << 3,
        IWM_MOTOR = 1 << 4,
        IWM_EXTERNAL = 1 << 5,
        IWM_Q6 = 1 << 6,
        IWM_Q7 = 1 << 7,
};

/* Asserted pins, represented as set bits despite their active-low polarity. */
enum {
        IWM_ENBL1 = 1 << 0,
        IWM_ENBL2 = 1 << 1,
};

struct iwm {
        struct disk_img *internal;
        uint64_t ticks;
        uint64_t motor_off_at;
        uint64_t clear_at;
        uint8_t switches;
        uint8_t mode;
        uint8_t shift;
        uint8_t latch;
        bool sel;
        bool warned_write;
};

/* Initialize with a newly opened drive, or NULL for an absent drive. */
void iwm_init(struct iwm *iwm, struct disk_img *internal, uint64_t ticks);
void iwm_reset(struct iwm *iwm, uint64_t ticks);
void iwm_advance(struct iwm *iwm, uint64_t ticks);
void iwm_set_sel(struct iwm *iwm, bool sel, uint64_t ticks);
uint8_t iwm_read(struct iwm *iwm, unsigned int address, uint64_t ticks);
void iwm_write(struct iwm *iwm, unsigned int address, uint8_t data, uint64_t ticks);

bool iwm_enabled(const struct iwm *iwm);
uint8_t iwm_drive_enables(const struct iwm *iwm);
unsigned int iwm_drive_register(const struct iwm *iwm);

#endif
