#ifndef DISK_IMG_H
#define DISK_IMG_H

#include <stdbool.h>
#include <stdint.h>

#define DISK_IMG_SIZE       (800 * 1024)
#define DISK_IMG_BIT_TICKS  16u

struct disk_img;

/* A time-ordered span of bit cells in the selected circular track. The
 * storage belongs to the drive and is valid until the next drive operation. */
struct disk_img_span {
        const uint8_t *bits;
        unsigned int length;
        unsigned int first;
        uint64_t count;
        uint64_t first_tick;
};

struct disk_img_status {
        unsigned int cylinder;
        unsigned int side;
        unsigned int bit_position;
        bool spindle;
        bool step_out;
        uint64_t step_done_at;
        uint64_t ready_at;
};

/* NULL path creates an empty internal drive. Images can be raw or DiskCopy
 * 4.2 Macintosh 800K. Returns NULL with errno set on error (EINVAL for
 * format, EBADMSG for checksum mismatch). */
struct disk_img *disk_img_open(const char *path);
void disk_img_close(struct disk_img *disk);

/* Advance before changing any of the drive inputs. The drive rotates while
 * its spindle is on, even if no read-data span is requested. */
struct disk_img_span disk_img_advance(struct disk_img *disk, uint64_t ticks, bool read_data);
/* Apply an effective /ENBL transition at the given tick. On disable,
 * stop the spindle and cancel any step still in progress at that tick. */
void disk_img_set_enabled(struct disk_img *disk, bool enabled, uint64_t ticks);
void disk_img_select_head(struct disk_img *disk, unsigned int side);
/* Sony command strobes are accepted while enabled. */
void disk_img_command(struct disk_img *disk, unsigned int reg, bool value);
bool disk_img_sense(const struct disk_img *disk, unsigned int reg);
struct disk_img_status disk_img_status(const struct disk_img *disk);

static inline unsigned int disk_img_bit(const struct disk_img_span *span, unsigned int position)
{
        return (span->bits[position / 8] >> (7 - position % 8)) & 1u;
}

#endif
