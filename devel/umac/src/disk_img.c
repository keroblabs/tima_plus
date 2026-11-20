#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
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
        MAX_SECTORS = 12,
        PAYLOAD_BYTES = TAG_SIZE + SECTOR_SIZE,
        PAYLOAD_CODES = (PAYLOAD_BYTES / 3) * 4 + 3 + 4,
        MARK_BYTES = 3, ADDRESS_BYTES = 12, FILL_BYTES = 2,
        FORMAT_ADDRESS_BYTES = ADDRESS_BYTES - 1, /* Its second FF starts the next sync group. */
        DATA_BYTES = MARK_BYTES + 1 + PAYLOAD_CODES + 2, /* Through DE AA. */
        FORMAT_START_GROUPS = 200,
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
static const uint8_t sync_group[] = {0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff};
static const uint8_t address_mark[] = {0xd5, 0xaa, 0x96};
static const uint8_t data_mark[] = {0xd5, 0xaa, 0xad};
static const uint8_t slip_mark[] = {0xde, 0xaa};

struct track {
        uint8_t bits[MAX_TRACK_BYTES];
        unsigned int length;
        unsigned int cylinder;
        unsigned int first_address;
        bool dirty;
        bool provisional;
};

struct write_capture {
        struct track candidate;
        /* Evidence in transmission order, including bits overwritten by
         * a format that wraps. A Plus ROM pass is shorter than two turns. */
        uint8_t emitted[2 * MAX_TRACK_BYTES];
        unsigned int first;
        unsigned int position;
        unsigned int count;
};

struct writable_image {
        char *path;
        char *lock_path;
        int lock_fd;
        int dir_fd;
        struct stat stamp;
        bool sync_pending;
};

struct disk_img {
        uint8_t *image;
        size_t image_size;
        const uint8_t *data;
        const uint8_t *tags;
        unsigned int tag_checksum_skip;
        struct writable_image *writable;
        struct write_capture *write;
        bool inserted;
        bool enabled;
        bool step_blocked;
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
        return MAX_SECTORS - cylinder / 16;
}

static unsigned int interleaved_sector(unsigned int slot, unsigned int sectors)
{
        return slot / 2 + (slot % 2) * ((sectors + 1) / 2);
}

static unsigned int first_block(unsigned int cylinder, unsigned int side)
{
        unsigned int block = 0;
        for (unsigned int track = 0; track < cylinder; ++track)
                block += 2 * sectors_per_side(track);
        return block + side * sectors_per_side(cylinder);
}

static void put_bit(uint8_t *bits, unsigned int position, unsigned int value)
{
        unsigned int mask = 0x80 >> (position % 8);
        if (value)
                bits[position / 8] |= mask;
        else
                bits[position / 8] &= ~mask;
}

static void append_bits(struct track *track, unsigned int value, unsigned int count)
{
        while (count) {
                put_bit(track->bits, track->length++, (value >> --count) & 1);
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

static struct track *encode_track(struct disk_img *disk)
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
        const unsigned int sector_bits = (ADDRESS_BYTES + sizeof(sync_group) + DATA_BYTES + FILL_BYTES) * 8;
        for (unsigned int slot = 0; slot < sectors; ++slot) {
                unsigned int sector = interleaved_sector(slot, sectors);
                unsigned int gap = (slot + 1) * length / sectors - track->length - sector_bits;
                unsigned int extra = gap % 10;
                append_bits(track, (1u << extra) - 1, extra);
                for (unsigned int i = 0; i < gap / 10; ++i)
                        append_bits(track, 0x3fc, 10);
                if (slot == 0)
                        track->first_address = track->length;
                unsigned int index = block + sector;
                const uint8_t *tag = disk->tags ? disk->tags + index * TAG_SIZE : empty_tag;
                append_sector(track, side, sector, tag, disk->data + index * SECTOR_SIZE);
        }
        return track;
}

static int decode_code(uint8_t code)
{
        const uint8_t *value = memchr(gcr, code, sizeof(gcr));
        return value ? (int)(value - gcr) : -1;
}

static bool decode_group(const uint8_t *codes, uint8_t *group, unsigned int count)
{
        int high = decode_code(*codes++);
        if (high < 0 || (count == 2 && (high & 3)))
                return false;
        for (unsigned int i = 0; i < count; ++i) {
                int low = decode_code(codes[i]);
                if (low < 0)
                        return false;
                group[i] = ((high >> (4 - 2 * i)) & 3) << 6 | low;
        }
        return true;
}

static bool decode_payload(const uint8_t *codes, uint8_t *payload)
{
        uint8_t checksum[3] = {0};
        for (unsigned int offset = 0; offset < PAYLOAD_BYTES;) {
                unsigned int count = PAYLOAD_BYTES - offset >= 3 ? 3 : 2;
                uint8_t group[3];
                if (!decode_group(codes, group, count))
                        return false;
                codes += 1 + count;
                unsigned int carry = checksum[2] >> 7;
                checksum[2] = (checksum[2] << 1) | carry;
                for (unsigned int i = 0; i < count; ++i) {
                        uint8_t byte = group[i] ^ checksum[(i + 2) % 3];
                        unsigned int sum = checksum[i] + byte + carry;
                        checksum[i] = sum;
                        carry = sum >> 8;
                        if (payload)
                                payload[offset + i] = byte;
                }
                offset += count;
        }

        uint8_t recorded[3];
        return decode_group(codes, recorded, 3) && !memcmp(recorded, checksum, sizeof(checksum));
}

static int decode_address(const uint8_t *record, unsigned int cylinder, unsigned int side)
{
        if (memcmp(record, address_mark, MARK_BYTES) ||
            memcmp(record + 8, slip_mark, sizeof(slip_mark)))
                return -1;

        int low = decode_code(record[3]);
        int sector = decode_code(record[4]);
        int high = decode_code(record[5]);
        int format = decode_code(record[6]);
        int check = decode_code(record[7]);
        if (low != (int)(cylinder & 0x3f) || high != (int)((cylinder >> 6) | (side << 5)) ||
            sector < 0 || sector >= (int)sectors_per_side(cylinder) || format != 0x22 ||
            check != (low ^ sector ^ high ^ format))
                return -1;
        return sector;
}

static bool decode_data(const uint8_t *record, int sector, uint8_t *payload)
{
        return !memcmp(record, data_mark, MARK_BYTES) && decode_code(record[MARK_BYTES]) == sector &&
               !memcmp(record + DATA_BYTES - sizeof(slip_mark), slip_mark, sizeof(slip_mark)) &&
               decode_payload(record + MARK_BYTES + 1, payload);
}

static unsigned int packed_bit(const uint8_t *bits, unsigned int position)
{
        return (bits[position / 8] >> (7 - position % 8)) & 1u;
}

static void track_bytes(const struct track *track, unsigned int position, uint8_t *bytes, size_t count)
{
        position %= track->length;
        unsigned int packed_size = (track->length + 7) / 8;
        while (count--) {
                unsigned int byte = position / 8;
                unsigned int offset = position % 8;
                uint8_t value = track->bits[byte] << offset;
                if (offset && byte + 1 < packed_size)
                        value |= track->bits[byte + 1] >> (8 - offset);
                if (position + 8 <= track->length) {
                        position += 8;
                        if (position == track->length)
                                position = 0;
                } else {
                        unsigned int tail = track->length - position;
                        value &= 0xffu << (8 - tail);
                        value |= track->bits[0] >> tail;
                        position = 8 - tail;
                }
                *bytes++ = value;
        }
}

/* Return the first address or data prologue at any bit alignment in the
 * given interval. Positions stay unwrapped so record limits are explicit. */
static unsigned int find_mark(const struct track *track, unsigned int start, unsigned int end)
{
        uint32_t window = 0;
        unsigned int pos = start % track->length;
        for (unsigned int i = start; i < end + MARK_BYTES * 8 - 1; ++i) {
                window = ((window << 1) | packed_bit(track->bits, pos)) & 0xffffff;
                if (window == 0xd5aa96 || window == 0xd5aaad)
                        return i + 1 - MARK_BYTES * 8;
                if (++pos == track->length)
                        pos = 0;
        }
        return end;
}

/* Decode a revolution starting at the original or newly transmitted first
 * address. Skip encoded payloads when looking for marks in the gaps. */
static bool decode_track(const struct track *track, unsigned int side,
                         uint8_t decoded[MAX_SECTORS][PAYLOAD_BYTES],
                         int written_sector, unsigned int written_mark)
{
        unsigned int seen = 0;
        bool found_write = written_sector < 0;
        unsigned int position = track->first_address;
        unsigned int end = position + track->length;
        unsigned int sectors = sectors_per_side(track->cylinder);
        while (position < end) {
                uint8_t address[ADDRESS_BYTES];
                track_bytes(track, position, address, sizeof(address));
                int sector = decode_address(address, track->cylinder, side);
                if (sector < 0 || (seen & (1u << sector)))
                        return false;

                unsigned int after_address = position + (ADDRESS_BYTES - FILL_BYTES) * 8;
                /* RdData searches for the data mark within 48 nibbles.
                 * Never associate data beyond an intervening address. */
                unsigned int limit = after_address + 48 * 10;
                if (limit > end)
                        limit = end;
                unsigned int mark = find_mark(track, after_address, limit);
                if (mark == limit || mark < after_address + sizeof(sync_group) * 8 ||
                    mark + DATA_BYTES * 8 > end)
                        return false;

                uint8_t sync[sizeof(sync_group)];
                uint8_t record[DATA_BYTES];
                track_bytes(track, mark - sizeof(sync) * 8, sync, sizeof(sync));
                track_bytes(track, mark, record, sizeof(record));
                if (memcmp(sync, sync_group, sizeof(sync)) ||
                    !decode_data(record, sector, decoded ? decoded[sector] : NULL))
                        return false;
                seen |= 1u << sector;
                if (sector == written_sector && mark % track->length == written_mark)
                        found_write = true;
                position = find_mark(track, mark + DATA_BYTES * 8, end);
        }
        return found_write && seen == (1u << sectors) - 1;
}

static bool is_filler(const struct write_capture *write, unsigned int used_bytes)
{
        unsigned int position = used_bytes * 8;
        if (write->count < position || write->count - position > FILL_BYTES * 8)
                return false;
        for (; position < write->count; ++position)
                if (!packed_bit(write->emitted, position))
                        return false;
        return true;
}

static bool validate_format(struct write_capture *write, unsigned int side, bool *gap_ok)
{
        const uint8_t *bytes = write->emitted;
        unsigned int size = write->count / 8;
        unsigned int position = 1; /* The ROM primes the shifter with a disposable byte. */
        unsigned int sectors = sectors_per_side(write->candidate.cylinder);
        for (unsigned int slot = 0; slot < sectors; ++slot) {
                unsigned int groups = 0;
                while (position + sizeof(sync_group) <= size &&
                       !memcmp(bytes + position, sync_group, sizeof(sync_group))) {
                        position += sizeof(sync_group);
                        ++groups;
                }
                if (groups <= (slot == 0 ? FORMAT_START_GROUPS : 0) ||
                    position + FORMAT_ADDRESS_BYTES + sizeof(sync_group) + DATA_BYTES > size)
                        return false;
                if (slot == 0) {
                        /* The next address is sector zero. The ROM accepts
                         * up to one sync group less at the closing gap. */
                        write->candidate.first_address = (write->first + position * 8) % write->candidate.length;
                        unsigned int min_gap = (groups - FORMAT_START_GROUPS - 1) * sizeof(sync_group) * 8;
                        *gap_ok = position * 8 + write->candidate.length >= write->count + min_gap;
                }
                const uint8_t *address = bytes + position;
                int sector = decode_address(address, write->candidate.cylinder, side);
                if (sector != (int)interleaved_sector(slot, sectors) || address[10] != 0xff)
                        return false;
                position += FORMAT_ADDRESS_BYTES;
                if (memcmp(bytes + position, sync_group, sizeof(sync_group)))
                        return false;
                position += sizeof(sync_group);
                if (!decode_data(bytes + position, sector, NULL))
                        return false;
                position += DATA_BYTES;
                if (slot + 1 < sectors) {
                        if (position + FILL_BYTES > size || bytes[position] != 0xff || bytes[position + 1] != 0xff)
                                return false;
                        position += FILL_BYTES;
                }
        }
        return is_filler(write, position);
}

static uint8_t *load_image(int fd, const struct stat *st)
{
        if (!S_ISREG(st->st_mode) ||
            (st->st_size != DISK_IMG_SIZE && st->st_size != DC42_DATA_END &&
             st->st_size != DC42_DATA_END + DC42_TAG_BYTES)) {
                errno = EINVAL;
                return NULL;
        }

        size_t size = st->st_size;
        uint8_t *image = malloc(size);
        if (!image)
                return NULL;
        for (size_t read_bytes = 0; read_bytes < size;) {
                ssize_t count = read(fd, image + read_bytes, size - read_bytes);
                if (count < 0 && errno == EINTR)
                        continue;
                if (count <= 0) {
                        int error = count ? errno : EIO;
                        free(image);
                        errno = error;
                        return NULL;
                }
                read_bytes += count;
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
        /* When both match (often a zero first tag), use Apple's convention. */
        if (tag_bytes && diskcopy_checksum(tags + TAG_SIZE, tag_bytes - TAG_SIZE) == tag_checksum)
                disk->tag_checksum_skip = TAG_SIZE;
        else if (diskcopy_checksum(tags, tag_bytes) != tag_checksum)
                return EBADMSG;

        disk->data = data;
        disk->tags = tag_bytes ? tags : NULL;
        return 0;
}

char *disk_img_lock_path(const char *path)
{
        size_t len = strlen(path) + sizeof(".iwm.lock");
        char *lock_path = malloc(len);
        if (lock_path)
                snprintf(lock_path, len, "%s.iwm.lock", path);
        return lock_path;
}

static bool same_inode(const struct stat *a, const struct stat *b)
{
        return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static bool same_snapshot(const struct stat *a, const struct stat *b)
{
        return same_inode(a, b) && S_ISREG(b->st_mode) && b->st_nlink == 1 &&
               a->st_size == b->st_size && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
               a->st_mtim.tv_nsec == b->st_mtim.tv_nsec;
}

static int check_lock(const struct writable_image *file)
{
        struct stat opened, named;
        if (fstat(file->lock_fd, &opened) < 0 || lstat(file->lock_path, &named) < 0)
                return -1;
        if (!S_ISREG(named.st_mode) || named.st_nlink != 1 || named.st_size != 0 ||
            !same_inode(&opened, &named)) {
                errno = ESTALE;
                return -1;
        }
        return 0;
}

static int check_image_path(const struct writable_image *file, struct stat *named)
{
        struct stat in_directory;
        const char *name = strrchr(file->path, '/') + 1;
        if (lstat(file->path, named) < 0 || fstatat(file->dir_fd, name, &in_directory, AT_SYMLINK_NOFOLLOW) < 0)
                return -1;
        if (!same_snapshot(&file->stamp, named) || !same_snapshot(named, &in_directory)) {
                errno = ESTALE;
                return -1;
        }
        return check_lock(file);
}

static int prepare_writable(struct disk_img *disk, const char *path)
{
        struct writable_image *file = malloc(sizeof(*file));
        if (!file)
                return -1;
        *file = (struct writable_image){ .lock_fd = -1, .dir_fd = -1 };
        disk->writable = file;
        file->path = realpath(path, NULL);
        if (!file->path)
                return -1;
        file->lock_path = disk_img_lock_path(file->path);
        if (!file->lock_path)
                return -1;
        char *separator = strrchr(file->path, '/');
        if (separator == file->path) {
                file->dir_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        } else {
                *separator = '\0';
                file->dir_fd = open(file->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
                *separator = '/';
        }
        if (file->dir_fd < 0)
                return -1;

        file->lock_fd = open(file->lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        if (file->lock_fd < 0 || check_lock(file) < 0 || flock(file->lock_fd, LOCK_EX | LOCK_NB) < 0)
                return -1;
        disk->write = malloc(sizeof(*disk->write));
        return disk->write ? 0 : -1;
}

static struct disk_img *open_disk(const char *path, bool writable)
{
        struct disk_img *disk = calloc(1, sizeof(*disk));
        if (!disk || !path)
                return disk;

        int flags = writable ? O_RDWR | O_NOFOLLOW : O_RDONLY;
        int fd = open(path, flags | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
                goto fail;

        struct stat opened;
        struct stat named;
        int error;
        if (fstat(fd, &opened) < 0)
                goto fail;
        if (writable && opened.st_nlink != 1) {
                errno = EMLINK;
                goto fail;
        }
        disk->image = load_image(fd, &opened);
        if (!disk->image)
                goto fail;
        disk->image_size = opened.st_size;
        disk->data = disk->image;
        if (disk->image_size != DISK_IMG_SIZE) {
                error = parse_diskcopy(disk, disk->image_size);
                if (error) {
                        errno = error;
                        goto fail;
                }
        }
        if (writable) {
                if (prepare_writable(disk, path) < 0)
                        goto fail;
                disk->writable->stamp = opened;
                if (check_image_path(disk->writable, &named) < 0)
                        goto fail;
        }
        close(fd);
        disk->inserted = true;
        return disk;

fail:
        error = errno;
        if (fd >= 0)
                close(fd);
        disk_img_close(disk);
        errno = error;
        return NULL;
}

struct disk_img *disk_img_open(const char *path)
{
        return open_disk(path, false);
}

struct disk_img *disk_img_open_writable(const char *path)
{
        if (!path || !*path) {
                errno = EINVAL;
                return NULL;
        }
        return open_disk(path, true);
}

static int write_all(int fd, const uint8_t *bytes, size_t size)
{
        while (size) {
                ssize_t count = write(fd, bytes, size);
                if (count < 0 && errno == EINTR)
                        continue;
                if (count <= 0) {
                        if (!count)
                                errno = EIO;
                        return -1;
                }
                bytes += count;
                size -= count;
        }
        return 0;
}

static int sync_file(int fd)
{
        int result;
        do {
                result = fsync(fd);
        } while (result < 0 && errno == EINTR);
        return result;
}

static int prepare_replacement(int fd, const struct stat *original, struct stat *replacement,
                               const uint8_t *bytes, size_t size)
{
        if (fstat(fd, replacement) < 0)
                return -1;
        if ((replacement->st_uid != original->st_uid || replacement->st_gid != original->st_gid) &&
            fchown(fd, original->st_uid, original->st_gid) < 0)
                return -1;
        if (write_all(fd, bytes, size) < 0 || fchmod(fd, original->st_mode & 07777) < 0 || sync_file(fd) < 0)
                return -1;
        return fstat(fd, replacement);
}

static int replace_image(struct writable_image *file, const uint8_t *bytes, size_t size)
{
        struct stat original, replacement;
        if (check_image_path(file, &original) < 0)
                return -1;

        size_t len = strlen(file->path) + sizeof(".tmp.XXXXXX");
        char *temporary = malloc(len);
        if (!temporary)
                return -1;
        snprintf(temporary, len, "%s.tmp.XXXXXX", file->path);
        int fd = mkstemp(temporary);
        if (fd < 0) {
                int error = errno;
                free(temporary);
                errno = error;
                return -1;
        }

        int result = prepare_replacement(fd, &original, &replacement, bytes, size);
        int error = errno;
        if (close(fd) < 0 && result == 0) {
                result = -1;
                error = errno;
        }
        if (result < 0) {
                unlink(temporary);
                free(temporary);
                errno = error;
                return -1;
        }

        result = -1;
        bool renamed = false;
        struct stat current;
        if (lstat(temporary, &current) < 0) {
                error = errno;
        } else if (!same_snapshot(&replacement, &current)) {
                error = ESTALE;
        } else if (check_image_path(file, &current) < 0 || rename(temporary, file->path) < 0) {
                error = errno;
        } else {
                renamed = true;
                /* On a directory-sync failure the replacement is already
                 * visible. Track its identity so a retry can safely replace
                 * it, while the previous logical snapshot remains committed. */
                file->stamp = replacement;
                file->sync_pending = true;
                if (sync_file(file->dir_fd) == 0 && check_image_path(file, &current) == 0) {
                        file->sync_pending = false;
                        result = 0;
                } else {
                        error = errno;
                }
        }
        if (!renamed)
                unlink(temporary);
        free(temporary);
        errno = error;
        return result;
}

static int stage_track(const struct disk_img *disk, unsigned int side, uint8_t *image)
{
        const struct track *track = &disk->tracks[side];
        uint8_t decoded[MAX_SECTORS][PAYLOAD_BYTES];
        if (track->provisional || !decode_track(track, side, decoded, -1, 0)) {
                errno = EBADMSG;
                return -1;
        }
        unsigned int block = first_block(track->cylinder, side);
        uint8_t *data = image + (disk->data - disk->image);
        static const uint8_t empty_tag[TAG_SIZE] = {0};
        /* Raw images store only sector data. Their guest tags remain in
         * the encoded track until it is rebuilt from the backing image. */
        for (unsigned int sector = 0; sector < sectors_per_side(track->cylinder); ++sector, ++block) {
                if (disk->tags) {
                        memcpy(image + DC42_DATA_END + block * TAG_SIZE, decoded[sector], TAG_SIZE);
                } else if (disk->image_size == DC42_DATA_END && memcmp(decoded[sector], empty_tag, TAG_SIZE)) {
                        errno = ENOTSUP;
                        return -1;
                }
                memcpy(data + block * SECTOR_SIZE, decoded[sector] + TAG_SIZE, SECTOR_SIZE);
        }
        return 0;
}

static int checkpoint_tracks(struct disk_img *disk, const char **detail)
{
        if (!disk || (!disk->tracks[0].dirty && !disk->tracks[1].dirty))
                return 0;

        uint8_t *image = malloc(disk->image_size);
        if (!image)
                return -1;
        memcpy(image, disk->image, disk->image_size);
        int error;
        for (unsigned int side = 0; side < 2; ++side) {
                if (disk->tracks[side].dirty && stage_track(disk, side, image) < 0) {
                        *detail = errno == ENOTSUP ? "tagless DiskCopy image cannot store nonzero sector tags" :
                                  "track format is incomplete or has an invalid circular gap";
                        goto fail;
                }
        }
        if (disk->image_size != DISK_IMG_SIZE) {
                WRITE_LONG(image, 72, diskcopy_checksum(image + DC42_HEADER_SIZE, DISK_IMG_SIZE));
                unsigned int skip = disk->tag_checksum_skip;
                unsigned int tag_bytes = disk->image_size - DC42_DATA_END;
                WRITE_LONG(image, 76, diskcopy_checksum(image + DC42_DATA_END + skip, tag_bytes - skip));
        }
        if ((disk->writable->sync_pending || memcmp(disk->image, image, disk->image_size)) &&
            replace_image(disk->writable, image, disk->image_size) < 0)
                goto fail;

        memcpy(disk->image, image, disk->image_size);
        free(image);
        disk->tracks[0].dirty = disk->tracks[1].dirty = false;
        return 0;

fail:
        error = errno;
        free(image);
        errno = error;
        return -1;
}

int disk_img_checkpoint(struct disk_img *disk)
{
        const char *detail = NULL;
        int result = checkpoint_tracks(disk, &detail);
        if (disk) {
                if (result < 0) {
                        int error = errno;
                        if (disk->status.checkpoint_error != error) {
                                fprintf(stderr, "IWM: cannot save floppy '%s' at cylinder %u: %s\n",
                                        disk->writable->path, disk->status.cylinder, detail ? detail : strerror(error));
                        }
                        disk->status.checkpoint_error = error;
                        errno = error;
                } else {
                        disk->status.checkpoint_error = 0;
                        disk->step_blocked = false;
                }
        }
        return result;
}

int disk_img_close(struct disk_img *disk)
{
        if (!disk)
                return 0;
        disk_img_abort_write(disk);
        if (disk_img_checkpoint(disk) < 0)
                return -1;

        struct writable_image *file = disk->writable;
        if (file) {
                if (file->lock_fd >= 0)
                        close(file->lock_fd);
                if (file->dir_fd >= 0)
                        close(file->dir_fd);
                free(file->lock_path);
                free(file->path);
                free(file);
        }
        free(disk->write);
        free(disk->image);
        free(disk);
        return 0;
}

static unsigned int revolution_ticks(unsigned int cylinder)
{
        return track_length(cylinder) * DISK_IMG_BIT_TICKS;
}

bool disk_img_begin_write(struct disk_img *disk, uint64_t first_tick)
{
        if (!disk || !disk->write || !disk->enabled || disk_img_sense(disk, 13) ||
            disk_img_writing(disk) || first_tick < disk->ticks)
                return false;

        struct write_capture *write = disk->write;
        write->candidate = *encode_track(disk);
        uint64_t phase = disk->rotation + (first_tick - disk->ticks) % revolution_ticks(disk->status.cylinder);
        write->first = write->position = (phase / DISK_IMG_BIT_TICKS) % write->candidate.length;
        write->count = 0;
        disk->status.write_bits = 0;
        disk->status.write_result = DISK_IMG_WRITE_CAPTURING;
        return true;
}

bool disk_img_writing(const struct disk_img *disk)
{
        return disk && disk->status.write_result == DISK_IMG_WRITE_CAPTURING;
}

void disk_img_write_bit(struct disk_img *disk, unsigned int bit)
{
        if (!disk_img_writing(disk))
                return;
        struct write_capture *write = disk->write;
        unsigned int capacity = sizeof(write->emitted) * 8;
        if (write->count >= capacity) {
                write->count = capacity + 1;
                return;
        }
        put_bit(write->candidate.bits, write->position, bit);
        put_bit(write->emitted, write->count++, bit);
        if (++write->position == write->candidate.length)
                write->position = 0;
        disk->status.write_bits = write->count;
}

void disk_img_abort_write(struct disk_img *disk)
{
        if (disk_img_writing(disk))
                disk->status.write_result = DISK_IMG_WRITE_ABORTED;
}

void disk_img_finish_write(struct disk_img *disk)
{
        if (!disk_img_writing(disk))
                return;
        struct write_capture *write = disk->write;
        unsigned int side = disk->status.side;
        bool valid = false;
        bool provisional = false;
        if (write->count <= sizeof(write->emitted) * 8) {
                if (is_filler(write, sizeof(sync_group) + DATA_BYTES) &&
                    !memcmp(write->emitted, sync_group, sizeof(sync_group))) {
                        const uint8_t *record = write->emitted + sizeof(sync_group);
                        int sector = decode_code(record[MARK_BYTES]);
                        unsigned int mark = (write->first + sizeof(sync_group) * 8) % write->candidate.length;
                        valid = sector >= 0 && decode_data(record, sector, NULL) &&
                                decode_track(&write->candidate, side, NULL, sector, mark);
                } else {
                        bool gap_ok = false;
                        valid = validate_format(write, side, &gap_ok);
                        provisional = valid && (!gap_ok || !decode_track(&write->candidate, side, NULL, -1, 0));
                }
        }

        struct track *active = &disk->tracks[side];
        if (!valid) {
                disk->status.write_result = DISK_IMG_WRITE_REJECTED;
                fprintf(stderr, "IWM: rejected incomplete or invalid floppy write at cylinder %u side %u (%u bits)\n",
                        disk->status.cylinder, side, disk->status.write_bits);
        } else if (active->provisional == provisional &&
                   !memcmp(active->bits, write->candidate.bits, (active->length + 7) / 8)) {
                disk->status.write_result = DISK_IMG_WRITE_UNCHANGED;
        } else {
                write->candidate.dirty = true;
                write->candidate.provisional = provisional;
                *active = write->candidate;
                disk->status.write_result = provisional ? DISK_IMG_WRITE_PROVISIONAL : DISK_IMG_WRITE_COMMITTED;
        }
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
                disk_img_abort_write(disk);
                disk->status.spindle = false;
                disk->status.ready_at = 0;
                disk->status.step_done_at = 0;
        }
}

void disk_img_select_head(struct disk_img *disk, unsigned int side)
{
        side &= 1;
        if (disk->status.side != side) {
                disk_img_abort_write(disk);
                disk->status.side = side;
        }
}

void disk_img_command(struct disk_img *disk, unsigned int reg, bool value)
{
        if (!disk->enabled)
                return;

        struct disk_img_status *status = &disk->status;
        switch (reg) {
        case 0: /* DIRTN */
                status->step_out = value;
                if (disk->step_blocked)
                        disk_img_checkpoint(disk); /* A subsequent seek may retry. */
                break;
        case 2: /* /STEP */
                if (!value && !status->step_done_at) {
                        disk_img_abort_write(disk);
                        unsigned int track = status->cylinder;
                        if (status->step_out && track > 0)
                                --track;
                        if (!status->step_out && track < CYLINDERS - 1)
                                ++track;
                        if (track != status->cylinder && disk_img_checkpoint(disk) < 0) {
                                disk->step_blocked = true;
                                break;
                        }
                        disk->target = track;
                        status->step_done_at = disk->ticks + STEP_TICKS;
                }
                break;
        case 4: /* /MOTORON */
                if (!value && !status->spindle)
                        status->ready_at = disk->ticks + SPIN_UP_TICKS;
                if (value)
                        disk_img_abort_write(disk);
                status->spindle = !value;
                break;
        case 6: /* EJECT */
                if (value) {
                        disk_img_abort_write(disk);
                        if (disk_img_checkpoint(disk) < 0)
                                break;
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
        case 2:  return !status->step_done_at && !disk->step_blocked;
        case 3:  return disk->inserted && disk->writable; /* /WRPROT */
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
                for (unsigned int side = 0; side < 2; ++side) {
                        status.dirty_sides |= (unsigned int)disk->tracks[side].dirty << side;
                        status.provisional_sides |= (unsigned int)disk->tracks[side].provisional << side;
                }
        }
        return status;
}
