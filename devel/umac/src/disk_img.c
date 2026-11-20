#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "disk_img.h"
#include "machw.h"

enum {
        CYLINDERS = 80, SECTOR_SIZE = 512, TAG_SIZE = 12,
        DC42_HEADER_SIZE = 84,
        DC42_DATA_END = DC42_HEADER_SIZE + DISK_IMG_SIZE,
        DC42_TAG_BYTES = DISK_IMG_SIZE / SECTOR_SIZE * TAG_SIZE,
        SPIN_UP_TICKS = MAC_C8M_HZ * 120 / 1000,
        STEP_TICKS = MAC_C8M_HZ * 3 / 1000,
        BITS_PER_MINUTE = MAC_C8M_HZ / DISK_IMG_BIT_TICKS * 60,
        MAX_TRACK_BYTES = (BITS_PER_MINUTE / 394 + 7) / 8,
};

/* Apple Technical Information Library TA45016 and the Plus ROM's Sony
 * format routines. The nominal RPMs set both track length and tach cadence. */
static const unsigned int rpm[] = {394, 429, 472, 525, 590};
static const uint8_t gcr[64] = {
        0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
        0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
        0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
        0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
        0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
        0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
        0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
        0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
};

struct track {
        uint8_t bits[MAX_TRACK_BYTES];
        unsigned int length;
        unsigned int cylinder;
};

struct disk_img {
        const uint8_t *image;
        const uint8_t *data;
        const uint8_t *tags;
        bool inserted;
        bool enabled;
        struct track tracks[2];
        struct disk_img_status status;
        unsigned int target;
        unsigned int rotation;
        uint64_t ticks;
};

static unsigned int track_length(unsigned int cylinder)
{
        return BITS_PER_MINUTE / rpm[cylinder / 16];
}

static unsigned int sectors_per_side(unsigned int cylinder)
{
        return 12 - cylinder / 16;
}

static unsigned int first_block(unsigned int cylinder, unsigned int side)
{
        unsigned int block = 0;
        for (unsigned int track = 0; track < cylinder; ++track)
                block += 2 * sectors_per_side(track);
        return block + side * sectors_per_side(cylinder);
}

static void append_bits(struct track *track, unsigned int value, unsigned int count)
{
        while (count) {
                unsigned int bit = track->length++;
                unsigned int mask = 0x80 >> (bit % 8);
                if (value & (1u << --count))
                        track->bits[bit / 8] |= mask;
                else
                        track->bits[bit / 8] &= ~mask;
        }
}

static void append_byte(struct track *track, uint8_t value)
{
        append_bits(track, value, 8);
}

/* A 6-and-2 group begins with the three high pairs, then the low six bits
 * of each byte. The final payload group has only two input bytes. */
static void append_group(struct track *track, const uint8_t group[3], unsigned int count)
{
        unsigned int high = ((group[0] >> 6) << 4) | ((group[1] >> 6) << 2) | (group[2] >> 6);
        append_byte(track, gcr[high]);
        for (unsigned int i = 0; i < count; ++i)
                append_byte(track, gcr[group[i] & 0x3f]);
}

static void append_payload(struct track *track, const uint8_t *tag, const uint8_t *sector)
{
        uint8_t checksum[3] = {0};
        for (unsigned int offset = 0; offset < TAG_SIZE + SECTOR_SIZE;) {
                uint8_t encoded[3] = {0};
                unsigned int carry = checksum[2] >> 7;
                checksum[2] = (checksum[2] << 1) | carry;
                unsigned int i = 0;
                while (i < 3 && offset < TAG_SIZE + SECTOR_SIZE) {
                        uint8_t byte = offset < TAG_SIZE ? tag[offset] : sector[offset - TAG_SIZE];
                        encoded[i] = byte ^ checksum[(i + 2) % 3];
                        unsigned int sum = checksum[i] + byte + carry;
                        checksum[i++] = sum;
                        carry = sum >> 8;
                        ++offset;
                }
                append_group(track, encoded, i);
        }
        append_group(track, checksum, 3);
}

static void append_marks(struct track *track, uint8_t mark)
{
        append_byte(track, 0xd5);
        append_byte(track, 0xaa);
        append_byte(track, mark);
}

static void append_trailer(struct track *track)
{
        append_byte(track, 0xde);
        append_byte(track, 0xaa);
        append_byte(track, 0xff);
        append_byte(track, 0xff);
}

static void append_sector(struct track *track, unsigned int side, unsigned int sector,
                          const uint8_t *tag, const uint8_t *data)
{
        unsigned int cylinder_lo = track->cylinder & 0x3f;
        unsigned int cylinder_hi_side = (track->cylinder >> 6) | (side << 5);
        const unsigned int format = 0x22; /* Double-sided, 2:1 interleave. */

        append_marks(track, 0x96);
        append_byte(track, gcr[cylinder_lo]);
        append_byte(track, gcr[sector]);
        append_byte(track, gcr[cylinder_hi_side]);
        append_byte(track, gcr[format]);
        append_byte(track, gcr[cylinder_lo ^ sector ^ cylinder_hi_side ^ format]);
        append_trailer(track);

        /* Apple's six packed sync bytes FF 3F CF F3 FC FF are four
         * ten-bit sync words followed by eight one bits. */
        for (unsigned int i = 0; i < 4; ++i)
                append_bits(track, 0x3fc, 10);
        append_byte(track, 0xff);
        append_marks(track, 0xad);
        append_byte(track, gcr[sector]);
        append_payload(track, tag, data);
        append_trailer(track);
}

static const struct track *encode_track(struct disk_img *disk)
{
        static const uint8_t empty_tag[TAG_SIZE] = {0};
        unsigned int cylinder = disk->status.cylinder;
        unsigned int side = disk->status.side;
        struct track *track = &disk->tracks[side];
        if (track->length && track->cylinder == cylinder)
                return track;

        track->length = 0;
        track->cylinder = cylinder;
        unsigned int sectors = sectors_per_side(cylinder);
        unsigned int length = track_length(cylinder);
        unsigned int block = first_block(cylinder, side);

        /* 12 address bytes, 48 sync bits, 4 data header bytes, 703 GCR
         * bytes and 4 trailer bytes. Put the remaining bits in address gaps. */
        const unsigned int sector_bits = (12 + 4 + 703 + 4) * 8 + 48;
        for (unsigned int slot = 0; slot < sectors; ++slot) {
                unsigned int sector = slot / 2 + (slot % 2) * ((sectors + 1) / 2);
                unsigned int gap = (slot + 1) * length / sectors - track->length - sector_bits;
                unsigned int extra = gap % 10;
                append_bits(track, (1u << extra) - 1, extra);
                for (unsigned int i = 0; i < gap / 10; ++i)
                        append_bits(track, 0x3fc, 10);
                unsigned int index = block + sector;
                const uint8_t *tag = disk->tags ? disk->tags + index * TAG_SIZE : empty_tag;
                append_sector(track, side, sector, tag, disk->data + index * SECTOR_SIZE);
        }
        return track;
}

static uint8_t *load_image(const char *path, size_t *size)
{
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
                return NULL;

        uint8_t *image = NULL;
        struct stat st;
        if (fstat(fd, &st) < 0)
                goto out;
        if (!S_ISREG(st.st_mode) ||
            (st.st_size != DISK_IMG_SIZE && st.st_size != DC42_DATA_END &&
             st.st_size != DC42_DATA_END + DC42_TAG_BYTES)) {
                errno = EINVAL;
                goto out;
        }

        *size = st.st_size;
        image = malloc(*size);
        if (!image)
                goto out;
        for (size_t read_bytes = 0; read_bytes < *size;) {
                ssize_t count = read(fd, image + read_bytes, *size - read_bytes);
                if (count < 0 && errno == EINTR)
                        continue;
                if (count <= 0) {
                        free(image);
                        image = NULL;
                        if (!count)
                                errno = EIO;
                        break;
                }
                read_bytes += count;
        }
out:
        {
                int saved_errno = errno;
                close(fd);
                errno = saved_errno;
        }
        return image;
}

static uint32_t diskcopy_checksum(const uint8_t *bytes, size_t length)
{
        uint32_t checksum = 0;
        for (size_t i = 0; i < length; i += 2) {
                checksum += READ_WORD(bytes, i);
                checksum = (checksum >> 1) | (checksum << 31);
        }
        return checksum;
}

static int parse_diskcopy(struct disk_img *disk, size_t size)
{
        const uint8_t *header = disk->image;
        uint32_t tag_bytes = READ_LONG(header, 68);
        /* Apple File Type Note $E0/$0005. Encoding 1 and format $22
         * identify Macintosh double-sided GCR with 2:1 interleave. */
        if (header[0] > 63 || READ_LONG(header, 64) != DISK_IMG_SIZE ||
            (tag_bytes && tag_bytes != DC42_TAG_BYTES) || size != DC42_DATA_END + tag_bytes ||
            header[80] != 1 || header[81] != 0x22 || READ_WORD(header, 82) != 0x0100)
                return EINVAL;

        const uint8_t *data = header + DC42_HEADER_SIZE;
        const uint8_t *tags = header + DC42_DATA_END;
        if (diskcopy_checksum(data, DISK_IMG_SIZE) != READ_LONG(header, 72))
                return EBADMSG;

        /* Apple images can omit the first sector tag from the checksum.
         * The first tag still belongs on the disk. */
        uint32_t tag_checksum = READ_LONG(header, 76);
        if (diskcopy_checksum(tags, tag_bytes) != tag_checksum &&
            (!tag_bytes || diskcopy_checksum(tags + TAG_SIZE, tag_bytes - TAG_SIZE) != tag_checksum))
                return EBADMSG;

        disk->data = data;
        disk->tags = tag_bytes ? tags : NULL;
        return 0;
}

struct disk_img *disk_img_open(const char *path)
{
        struct disk_img *disk = calloc(1, sizeof(*disk));
        if (disk && path) {
                size_t size;
                disk->image = load_image(path, &size);
                if (!disk->image) {
                        free(disk);
                        return NULL;
                }
                disk->data = disk->image;
                if (size != DISK_IMG_SIZE) {
                        int error = parse_diskcopy(disk, size);
                        if (error) {
                                disk_img_close(disk);
                                errno = error;
                                return NULL;
                        }
                }
                disk->inserted = true;
        }
        return disk;
}

void disk_img_close(struct disk_img *disk)
{
        if (disk) {
                free((void *)disk->image);
                free(disk);
        }
}

static unsigned int revolution_ticks(unsigned int cylinder)
{
        return track_length(cylinder) * DISK_IMG_BIT_TICKS;
}

static void rotate_to(struct disk_img *disk, uint64_t ticks)
{
        if (disk->status.spindle) {
                unsigned int period = revolution_ticks(disk->status.cylinder);
                disk->rotation = (disk->rotation + (ticks - disk->ticks) % period) % period;
        }
        disk->ticks = ticks;
}

struct disk_img_span disk_img_advance(struct disk_img *disk, uint64_t ticks, bool read_data)
{
        struct disk_img_span span = {0};
        if (!disk || ticks <= disk->ticks)
                return span;

        struct disk_img_status *status = &disk->status;
        if (status->step_done_at && ticks >= status->step_done_at) {
                rotate_to(disk, status->step_done_at);
                disk->rotation = (uint64_t)disk->rotation * revolution_ticks(disk->target) /
                                 revolution_ticks(status->cylinder);
                status->cylinder = disk->target;
                status->step_done_at = 0;
        }

        if (read_data && disk->inserted && status->spindle &&
            !status->step_done_at && ticks >= status->ready_at) {
                if (status->ready_at > disk->ticks)
                        rotate_to(disk, status->ready_at);
                unsigned int until_bit = DISK_IMG_BIT_TICKS - disk->rotation % DISK_IMG_BIT_TICKS;
                if (ticks - disk->ticks >= until_bit) {
                        const struct track *track = encode_track(disk);
                        span = (struct disk_img_span){
                                .bits = track->bits, .length = track->length,
                                .first = disk->rotation / DISK_IMG_BIT_TICKS,
                                .first_tick = disk->ticks + until_bit,
                        };
                        span.count = 1 + (ticks - span.first_tick) / DISK_IMG_BIT_TICKS;
                }
        }

        rotate_to(disk, ticks);
        return span;
}

void disk_img_set_enabled(struct disk_img *disk, bool enabled, uint64_t ticks)
{
        if (!disk || disk->enabled == enabled)
                return;

        /* Finish any step due by the transition and preserve rotational phase. */
        disk_img_advance(disk, ticks, false);
        disk->enabled = enabled;
        if (!enabled) {
                disk->status.spindle = false;
                disk->status.ready_at = 0;
                disk->status.step_done_at = 0;
        }
}

void disk_img_select_head(struct disk_img *disk, unsigned int side)
{
        disk->status.side = side & 1;
}

void disk_img_command(struct disk_img *disk, unsigned int reg, bool value)
{
        if (!disk->enabled)
                return;

        struct disk_img_status *status = &disk->status;
        switch (reg) {
        case 0: /* DIRTN */
                status->step_out = value;
                break;
        case 2: /* /STEP */
                if (!value && !status->step_done_at) {
                        unsigned int track = status->cylinder;
                        if (status->step_out && track > 0)
                                --track;
                        if (!status->step_out && track < CYLINDERS - 1)
                                ++track;
                        disk->target = track;
                        status->step_done_at = disk->ticks + STEP_TICKS;
                }
                break;
        case 4: /* /MOTORON */
                if (!value && !status->spindle)
                        status->ready_at = disk->ticks + SPIN_UP_TICKS;
                status->spindle = !value;
                break;
        case 6: /* EJECT */
                if (value) {
                        disk->inserted = false;
                        status->spindle = false;
                }
                break;
        }
}

bool disk_img_sense(const struct disk_img *disk, unsigned int reg)
{
        if (!disk)
                return true; /* Pulled up when the drive is absent or disabled. */

        const struct disk_img_status *status = &disk->status;
        switch (reg) {
        case 0:  return status->step_out;
        case 1:  return !disk->inserted;
        case 2:  return !status->step_done_at;
        case 3:  return !disk->inserted; /* /WRPROT: always protected when inserted. */
        case 4:  return !status->spindle;
        case 5:  return status->cylinder != 0;
        case 7:  return status->spindle && disk->ticks >= status->ready_at &&
                        ((120u * disk->rotation / revolution_ticks(status->cylinder)) & 1);
        case 13: return !disk->inserted || !status->spindle || status->step_done_at ||
                        disk->ticks < status->ready_at;
        case 14: return false; /* /DRVEXST */
        default: return true;  /* Idle EJECT, SIDES=1, new-interface=1, reserved inputs. */
        }
}

struct disk_img_status disk_img_status(const struct disk_img *disk)
{
        struct disk_img_status status = {0};
        if (disk) {
                status = disk->status;
                status.bit_position = disk->rotation / DISK_IMG_BIT_TICKS;
        }
        return status;
}
