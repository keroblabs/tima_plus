#include <stdio.h>

#include "disk_img.h"
#include "iwm.h"

enum {
        MODE_LATCH = 0x01,
        MODE_ASYNC = 0x02,
        MODE_TIMER_DISABLE = 0x04,
        MODE_FAST = 0x08,
        MODE_C8M = 0x10,
        READ_MODE = MODE_LATCH | MODE_ASYNC | MODE_FAST | MODE_C8M,
        /* Apple IWM Rev. 19, data separator and motor timer. Apple Guide to
         * the Macintosh Family Hardware fig. 9-5 ties Plus FCLK to C8M. */
        CLEAR_TICKS = 14,
        MOTOR_OFF_TICKS = 1 << 23,
};

bool iwm_enabled(const struct iwm *iwm)
{
        return (iwm->switches & IWM_MOTOR) || iwm->motor_off_at;
}

uint8_t iwm_drive_enables(const struct iwm *iwm)
{
        if (!iwm_enabled(iwm))
                return 0;
        return iwm->switches & IWM_EXTERNAL ? IWM_ENBL2 : IWM_ENBL1;
}

static struct disk_img *selected_drive(const struct iwm *iwm)
{
        return iwm_drive_enables(iwm) & IWM_ENBL1 ? iwm->internal : NULL;
}

static void update_drive_enables(struct iwm *iwm, uint8_t old_enables)
{
        uint8_t enables = iwm_drive_enables(iwm);
        if ((old_enables ^ enables) & IWM_ENBL1)
                disk_img_set_enabled(iwm->internal, (enables & IWM_ENBL1) != 0, iwm->ticks);
}

unsigned int iwm_drive_register(const struct iwm *iwm)
{
        return ((iwm->switches & (IWM_PHASE0 | IWM_PHASE1 | IWM_PHASE2)) << 1) | iwm->sel;
}

static bool reading_track(const struct iwm *iwm)
{
        return selected_drive(iwm) && !(iwm->switches & (IWM_STROBE | IWM_Q6 | IWM_Q7)) &&
               (iwm->mode & READ_MODE) == READ_MODE && (iwm_drive_register(iwm) & ~1u) == 8;
}

static void rearm(struct iwm *iwm)
{
        iwm->shift = 0;
        iwm->latch = 0;
        iwm->clear_at = 0;
}

static void clear_acknowledged_byte(struct iwm *iwm, uint64_t ticks)
{
        if (iwm->clear_at && ticks >= iwm->clear_at) {
                /* D7 is the valid bit cleared by the read acknowledgement. */
                iwm->latch &= 0x7f;
                iwm->clear_at = 0;
        }
}

static void shift_bits(struct iwm *iwm, struct disk_img_span span)
{
        /* After a full revolution the sync fields have established phase
         * regardless of earlier data. Bound work after long gaps in access. */
        if (span.count > span.length) {
                uint64_t skip = span.count - span.length;
                span.first = (span.first + skip % span.length) % span.length;
                span.first_tick += skip * DISK_IMG_BIT_TICKS;
                span.count = span.length;
                rearm(iwm);
        }

        unsigned int position = span.first;
        uint64_t tick = span.first_tick;
        for (uint64_t i = 0; i < span.count; ++i, tick += DISK_IMG_BIT_TICKS) {
                clear_acknowledged_byte(iwm, tick);
                iwm->shift = (iwm->shift << 1) | disk_img_bit(&span, position);
                if (++position == span.length)
                        position = 0;
                if (iwm->shift & 0x80) {
                        /* Each valid codeword replaces the single latch. A
                         * pending clear belongs to the acknowledged byte. */
                        iwm->latch = iwm->shift;
                        iwm->shift = 0;
                        iwm->clear_at = 0;
                }
        }
}

void iwm_advance(struct iwm *iwm, uint64_t ticks)
{
        if (ticks <= iwm->ticks)
                return;

        if (iwm->motor_off_at && ticks >= iwm->motor_off_at) {
                uint8_t old_enables = iwm_drive_enables(iwm);
                iwm->ticks = iwm->motor_off_at;
                shift_bits(iwm, disk_img_advance(iwm->internal, iwm->ticks, reading_track(iwm)));
                iwm->motor_off_at = 0;
                update_drive_enables(iwm, old_enables);
                rearm(iwm);
        }
        shift_bits(iwm, disk_img_advance(iwm->internal, ticks, reading_track(iwm)));
        clear_acknowledged_byte(iwm, ticks);
        iwm->ticks = ticks;
}

void iwm_init(struct iwm *iwm, struct disk_img *internal, uint64_t ticks)
{
        disk_img_advance(internal, ticks, false);
        *iwm = (struct iwm){ .internal = internal, .ticks = ticks };
}

void iwm_reset(struct iwm *iwm, uint64_t ticks)
{
        iwm_advance(iwm, ticks);
        uint8_t old_enables = iwm_drive_enables(iwm);
        iwm_init(iwm, iwm->internal, iwm->ticks);
        update_drive_enables(iwm, old_enables);
}

static void update_head(struct iwm *iwm, bool was_reading, unsigned int old_reg)
{
        struct disk_img *disk = selected_drive(iwm);
        unsigned int reg = iwm_drive_register(iwm);
        if (disk && !(iwm->switches & IWM_STROBE) && (reg & ~1u) == 8)
                disk_img_select_head(disk, reg & 1);
        if (reading_track(iwm) != was_reading || (was_reading && reg != old_reg))
                rearm(iwm);
}

void iwm_set_sel(struct iwm *iwm, bool sel, uint64_t ticks)
{
        iwm_advance(iwm, ticks);
        bool was_reading = reading_track(iwm);
        unsigned int old_reg = iwm_drive_register(iwm);
        iwm->sel = sel;
        update_head(iwm, was_reading, old_reg);
}

static void apply_switch(struct iwm *iwm, unsigned int address, uint64_t ticks)
{
        iwm_advance(iwm, ticks);
        unsigned int index = (address >> 9) & 15;
        unsigned int bit = 1u << (index >> 1);
        uint8_t old = iwm->switches;
        unsigned int next = (index & 1) ? old | bit : old & ~bit;
        if (next == old)
                return;

        uint8_t old_enables = iwm_drive_enables(iwm);
        bool was_reading = reading_track(iwm);
        unsigned int old_reg = iwm_drive_register(iwm);
        iwm->switches = next;
        if (bit == IWM_MOTOR) {
                iwm->motor_off_at = 0;
                if (!(next & IWM_MOTOR) && !(iwm->mode & MODE_TIMER_DISABLE))
                        iwm->motor_off_at = iwm->ticks + MOTOR_OFF_TICKS;
        }
        update_drive_enables(iwm, old_enables);

        struct disk_img *disk = selected_drive(iwm);
        if (disk && bit == IWM_STROBE && (next & IWM_STROBE))
                disk_img_command(disk, iwm_drive_register(iwm) & 7, next & IWM_PHASE2);
        update_head(iwm, was_reading, old_reg);
}

uint8_t iwm_read(struct iwm *iwm, unsigned int address, uint64_t ticks)
{
        apply_switch(iwm, address, ticks);
        switch (iwm->switches & (IWM_Q6 | IWM_Q7)) {
        case 0:
                if (!iwm_enabled(iwm))
                        return 0xff;
                if ((iwm->latch & 0x80) && !iwm->clear_at)
                        iwm->clear_at = iwm->ticks + CLEAR_TICKS;
                return iwm->latch;
        case IWM_Q6: {
                bool sense = disk_img_sense(selected_drive(iwm), iwm_drive_register(iwm));
                return (sense ? 0x80 : 0) | (iwm_enabled(iwm) ? 0x20 : 0) | iwm->mode;
        }
        case IWM_Q7:
                return 0x3f; /* Write stub: no buffer ready or successful transfer. */
        default:
                return 0xff;
        }
}

void iwm_write(struct iwm *iwm, unsigned int address, uint8_t data, uint64_t ticks)
{
        apply_switch(iwm, address, ticks);
        if ((iwm->switches & (IWM_Q6 | IWM_Q7)) != (IWM_Q6 | IWM_Q7))
                return;
        if (!iwm_enabled(iwm)) {
                iwm->mode = data & 0x1f;
                rearm(iwm);
        } else if (!iwm->warned_write) {
                fputs("IWM: rejected write to read-only floppy\n", stderr);
                iwm->warned_write = true;
        }
}
