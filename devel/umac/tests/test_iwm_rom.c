#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "b2_macos_util.h"
#include "m68k.h"
#include "umac.h"

#define CHECK(expr) do {                                                      \
        if (!(expr)) {                                                        \
                fprintf(stderr, "%s:%d: %s (PC=$%06x)\n",                      \
                        __FILE__, __LINE__, #expr, m68k_get_reg(NULL, M68K_REG_PC)); \
                exit(EXIT_FAILURE);                                           \
        }                                                                     \
} while (0)

enum {
        SECTOR_BYTES = 512, TAG_BYTES = 12,
        DISKCOPY_HEADER = 84,
        DISKCOPY_SIZE = DISKCOPY_HEADER + DISK_IMG_SIZE,
        TAGGED_DISKCOPY_SIZE = DISKCOPY_SIZE + 1600 * TAG_BYTES,
        PB_OFFSET = 16, SECTOR_BUFFER_OFFSET = 128, TAG_BUFFER_OFFSET = SECTOR_BUFFER_OFFSET + SECTOR_BYTES,
        /* Inside Macintosh, Appendix C, Global Variables. */
        KEY_MAP = 0x174, SCREEN_BASE = 0x824, RAW_MOUSE_X = 0x82e, CUR_AP_NAME = 0x910,
        /* Plus V3 .Sony CtlFormat starts GapSync at 7 in the SonyVars block. */
        SONY_VARS = 0x134, GAP_SYNC = 0x22,
        SONY_DRIVE_1 = 0x4a, SONY_DISK_IN_PLACE = 3,
        SONY_READ_VERIFY = 1 << 6, SONY_DATA_VERIFY_ERR = -68,
};

enum sony_io { SONY_READ, SONY_WRITE, SONY_VERIFY };

static uint64_t next_vbl = MAC_FRAME_TICKS;
static uint64_t next_second = MAC_C8M_HZ;
static char writable_path[] = "/tmp/umac-iwm-rom-XXXXXX";

#ifdef UMAC_TEST_LINK_WRAP
static bool protected_during_boot;
static bool corrupt_next_payload;
bool __real_disk_img_sense(const struct disk_img *disk, unsigned int reg);
void __real_disk_img_write_bit(struct disk_img *disk, unsigned int bit);

bool __wrap_disk_img_sense(const struct disk_img *disk, unsigned int reg)
{
        /* Simulate the write-protect tab on the tagless DiskCopy fixture until
         * Finder is ready. The low-level write test runs with normal sense. */
        return (protected_during_boot && disk && reg == 3) ? false : __real_disk_img_sense(disk, reg);
}

void __wrap_disk_img_write_bit(struct disk_img *disk, unsigned int bit)
{
        if (corrupt_next_payload && disk_img_status(disk).write_bits == 10 * 8) {
                /* The first payload symbol must have its high bit set. */
                bit = 0;
                corrupt_next_payload = false;
        }
        __real_disk_img_write_bit(disk, bit);
}
#endif

static void step(void)
{
        CHECK(umac_loop() == 0);
        uint64_t ticks = umac_ticks();
        if (ticks >= next_vbl) {
                umac_vsync_event();
                next_vbl += MAC_FRAME_TICKS;
        }
        if (ticks >= next_second) {
                umac_1hz_event();
                next_second += MAC_C8M_HZ;
        }
}

static void boot_finder(void)
{
        uint64_t start = umac_ticks();
        uint64_t finder_at = 0;
        while (umac_ticks() - start < 110 * MAC_C8M_HZ) {
                step();
                if (overlay || memcmp(ram_get_base() + CUR_AP_NAME, "\006Finder", 7))
                        continue;
                if (!finder_at)
                        finder_at = umac_ticks();
                /* Wait for Finder to be executing from its loaded application
                 * segments after the Segment Manager sets CurApName. */
                if (umac_ticks() - finder_at >= 8 * MAC_C8M_HZ &&
                    m68k_get_reg(NULL, M68K_REG_PC) < RAM_SIZE &&
                    !(m68k_get_reg(NULL, M68K_REG_SR) & 0x0700)) {
                        printf("Finder executing at C8M tick %" PRIu64 "\n", umac_ticks());
                        return;
                }
        }
        CHECK(finder_at && umac_ticks() < start + 110 * MAC_C8M_HZ);
}

static size_t read_file(const char *path, uint8_t *bytes, size_t capacity)
{
        FILE *file = fopen(path, "rb");
        CHECK(file);
        size_t size = fread(bytes, 1, capacity, file);
        CHECK(fgetc(file) == EOF && !ferror(file));
        CHECK(fclose(file) == 0);
        return size;
}

static void test_display_and_input(void)
{
        CHECK(RAM_RD32(SCREEN_BASE) == umac_get_fb_offset());
        const uint8_t *screen = ram_get_base() + umac_get_fb_offset();
        size_t screen_bytes = MAC_SCREEN_WIDTH * MAC_SCREEN_HEIGHT / 8;
        CHECK(memchr(screen, 0x00, screen_bytes) && memchr(screen, 0xff, screen_bytes));

        uint16_t old_x = RAM_RD16(RAW_MOUSE_X);
        umac_mouse(12, 0, 0);
        for (unsigned int frame = 0; frame < 100 && RAM_RD16(RAW_MOUSE_X) == old_x; ++frame)
                step();
        CHECK(RAM_RD16(RAW_MOUSE_X) > old_x);

        uint8_t old_keys = RAM_RD8(KEY_MAP);
        umac_kbd_event(0x01, 1); /* A key down. */
        for (unsigned int frame = 0; frame < 200 && RAM_RD8(KEY_MAP) == old_keys; ++frame)
                step();
        CHECK(RAM_RD8(KEY_MAP) != old_keys);
        umac_kbd_event(0x01, 0);
        for (unsigned int frame = 0; frame < 200 && RAM_RD8(KEY_MAP) != old_keys; ++frame)
                step();
        CHECK(RAM_RD8(KEY_MAP) == old_keys);
}

static void run_trap(unsigned int code, uint16_t trap, unsigned int seconds, struct disk_img *format_disk)
{
        RAM_WR16(code, trap);
        RAM_WR16(code + 2, 0x60fe); /* BRA.S to self. */
        m68k_set_reg(M68K_REG_PC, code);
        uint64_t deadline = umac_ticks() + (uint64_t)seconds * MAC_C8M_HZ;
        bool forced_gap = false;
        bool saw_provisional = false;
        bool saw_retry = false;
        do {
                step();
                if (format_disk) {
                        struct disk_img_status status = disk_img_status(format_disk);
                        unsigned int gap = RAM_RD32(SONY_VARS) + GAP_SYNC;
                        /* Approach cylinder zero from afar. Increase the initial
                         * gap in guest RAM after CtlFormat initializes it. */
                        if (!forced_gap && status.cylinder > 0 && RAM_RD16(gap) == 7) {
                                RAM_WR16(gap, 11);
                                forced_gap = true;
                        }
                        if (!saw_provisional && status.provisional_sides) {
                                CHECK(forced_gap && status.cylinder == 0);
                                CHECK(disk_img_checkpoint(format_disk) < 0 && errno == EBADMSG);
                                saw_provisional = true;
                        }
                        if (saw_provisional && status.cylinder == 0 &&
                            (status.dirty_sides & 1) && !(status.provisional_sides & 1))
                                saw_retry = true;
                }
        } while (umac_ticks() < deadline && m68k_get_reg(NULL, M68K_REG_PC) != code + 2);

        CHECK(umac_ticks() < deadline);
        if (format_disk) {
                CHECK(forced_gap && saw_provisional && saw_retry);
                puts("Original ROM retried a provisional format after its first address was overwritten");
        }
}

static void sony_control(unsigned int scratch, unsigned int command, unsigned int param, struct disk_img *format_disk)
{
        unsigned int pb = scratch + PB_OFFSET;
        memset(ram_get_base() + pb, 0, 64);
        RAM_WR16(pb + ioVRefNum, 1);
        RAM_WR16(pb + ioRefNum, -5);
        RAM_WR16(pb + csCode, command);
        RAM_WR32(pb + csParam, param);
        m68k_set_reg(M68K_REG_A0, pb);
        run_trap(scratch, 0xa004, command == 5 || command == 6 ? 120 : 8, format_disk);
        int result = (int16_t)RAM_RD16(pb + ioResult);
        if (result)
                fprintf(stderr, ".Sony _Control %u returned %d\n", command, result);
        CHECK(result == noErr);
}

static void sony_request(unsigned int scratch, unsigned int block, const uint8_t *data,
                         const uint8_t *tags, enum sony_io operation, int expected_result)
{
        static const uint8_t empty_tag[TAG_BYTES] = {0};
        unsigned int pb = scratch + PB_OFFSET;
        unsigned int buffer = scratch + SECTOR_BUFFER_OFFSET;
        uint8_t *ram = ram_get_base();
        const uint8_t *sector = data + block * SECTOR_BYTES;
        const uint8_t *tag = tags ? tags + block * TAG_BYTES : empty_tag;
        memset(ram + pb, 0, 64);
        memset(ram + buffer, 0xa5, SECTOR_BYTES + TAG_BYTES);
        RAM_WR16(pb + ioVRefNum, 1);
        RAM_WR16(pb + ioRefNum, -5);
        RAM_WR32(pb + ioBuffer, buffer);
        RAM_WR32(pb + ioReqCount, SECTOR_BYTES);
        RAM_WR16(pb + ioPosMode, fsFromStart | (operation == SONY_VERIFY ? SONY_READ_VERIFY : 0));
        RAM_WR32(pb + ioPosOffset, block * SECTOR_BYTES);
        if (operation != SONY_READ) {
                memcpy(ram + buffer, sector, SECTOR_BYTES);
                memcpy(ram + scratch + TAG_BUFFER_OFFSET, tag, TAG_BYTES);
        }
        m68k_set_reg(M68K_REG_A0, pb);
        run_trap(scratch, operation == SONY_WRITE ? 0xa003 : 0xa002, 8, NULL);

        int result = (int16_t)RAM_RD16(pb + ioResult);
        if (result != expected_result)
                fprintf(stderr, ".Sony operation %d returned %d for block %u\n", operation, result, block);
        CHECK(result == expected_result && RAM_RD32(pb + ioActCount) == (result ? 0 : SECTOR_BYTES));
        if (operation == SONY_READ) {
                CHECK(!memcmp(ram + buffer, sector, SECTOR_BYTES));
                CHECK(!memcmp(ram + scratch + TAG_BUFFER_OFFSET, tag, TAG_BYTES));
        }
}

static void remove_writable_image(void)
{
        char *lock = disk_img_lock_path(writable_path);
        if (lock) {
                unlink(lock);
                free(lock);
        }
        unlink(writable_path);
}

static uint32_t checksum(const uint8_t *bytes, size_t count)
{
        uint32_t sum = 0;
        for (size_t i = 0; i < count; i += 2) {
                sum += bytes[i] * 256 + bytes[i + 1];
                sum = (sum >> 1) | (sum << 31);
        }
        return sum;
}

static void set_expected_checksums(uint8_t *image, size_t size, unsigned int skip)
{
        if (size != DISK_IMG_SIZE) {
                WRITE_LONG(image, 72, checksum(image + DISKCOPY_HEADER, DISK_IMG_SIZE));
                WRITE_LONG(image, 76, checksum(image + DISKCOPY_SIZE + skip, size - DISKCOPY_SIZE - skip));
        }
}

static void make_writable_image(const uint8_t *bytes, size_t size)
{
        int fd = mkstemp(writable_path);
        CHECK(fd >= 0);
        atexit(remove_writable_image);
        FILE *file = fdopen(fd, "wb");
        CHECK(file && fwrite(bytes, 1, size, file) == size && fclose(file) == 0);
}

static void test_writable(struct disk_img *disk, unsigned int scratch, uint8_t *expected, uint8_t *after,
                          size_t size, bool format)
{
        /* Finder may update the volume before the probes begin. Use a
         * checkpoint of the booted fixture as the write comparison base. */
        uint8_t *before = malloc(size);
        CHECK(before && disk_img_checkpoint(disk) == 0);
        CHECK(read_file(writable_path, before, size) == size);
        memcpy(expected, before, size);
        uint8_t *data = expected + (size == DISK_IMG_SIZE ? 0 : DISKCOPY_HEADER);
        uint8_t *tags = size == TAGGED_DISKCOPY_SIZE ? data + DISK_IMG_SIZE : NULL;
        unsigned int skip = tags && checksum(tags + TAG_BYTES, 1599 * TAG_BYTES) == READ_LONG(expected, 76)
                            ? TAG_BYTES : 0;
        CHECK(disk_img_sense(disk, 3));
        sony_control(scratch, 9, 0, NULL); /* Disable the guest track cache. */
        sony_control(scratch, 8, scratch + TAG_BUFFER_OFFSET, NULL);
        sony_request(scratch, 0, data, tags, SONY_READ, noErr);
        if (format) {
                sony_request(scratch, 1599, data, tags, SONY_READ, noErr);
                sony_control(scratch, 6, 0, disk);
                CHECK(disk_img_status(disk).cylinder == 79);
                sony_control(scratch, 5, 0, NULL);
                memset(data, 0, size - (data - expected));
                puts("Original ROM format and verify passed across all 80 cylinders and both heads");
        } else {
#ifdef UMAC_TEST_LINK_WRAP
                /* The transport succeeded, but the backend rejected a bad
                 * GCR symbol at Q7L. The ROM's read-verify detects old data. */
                data[0] ^= 0xff;
                corrupt_next_payload = true;
                sony_request(scratch, 0, data, tags, SONY_WRITE, noErr);
                CHECK(!corrupt_next_payload && disk_img_status(disk).write_result == DISK_IMG_WRITE_REJECTED);
                CHECK(!disk_img_status(disk).dirty_sides);
                sony_request(scratch, 0, data, tags, SONY_VERIFY, SONY_DATA_VERIFY_ERR);
                data[0] ^= 0xff;
                sony_request(scratch, 0, data, tags, SONY_VERIFY, noErr);
                puts("Original ROM read-verify detected a rejected candidate as unchanged media");
#endif
                uint8_t *raw_tags = NULL;
                if (size == DISK_IMG_SIZE) {
                        raw_tags = calloc(1600, TAG_BYTES);
                        CHECK(raw_tags);
                }
                uint8_t *written_tags = tags ? tags : raw_tags;
                const unsigned int blocks[] = {0, 12};
                for (unsigned int i = 0; i < 2; ++i) {
                        unsigned int block = blocks[i];
                        for (unsigned int j = 0; j < SECTOR_BYTES; ++j)
                                data[block * SECTOR_BYTES + j] ^= 0xa5;
                        if (written_tags)
                                for (unsigned int j = 0; j < TAG_BYTES; ++j)
                                        written_tags[block * TAG_BYTES + j] ^= 0x5a;
                        sony_request(scratch, block, data, written_tags, SONY_WRITE, noErr);
                        CHECK(disk_img_status(disk).write_result == DISK_IMG_WRITE_COMMITTED);
                        sony_request(scratch, block, data, written_tags, SONY_READ, noErr);
                        CHECK(disk_img_status(disk).dirty_sides == (i ? 3u : 1u));
                }
                CHECK(read_file(writable_path, after, TAGGED_DISKCOPY_SIZE) == size);
                CHECK(!memcmp(after, before, size));
                sony_request(scratch, 24, data, tags, SONY_READ, noErr); /* Step to cylinder one. */
                CHECK(!disk_img_status(disk).dirty_sides);
                puts("Original ROM wrote and read both sides before a seek saved them");
                if (raw_tags) {
                        /* Read both sides of cylinder one to replace the two
                         * cached tracks before reading cylinder zero again. */
                        sony_request(scratch, 36, data, NULL, SONY_READ, noErr);
                        sony_request(scratch, 0, data, NULL, SONY_READ, noErr);
                        sony_request(scratch, 12, data, NULL, SONY_READ, noErr);
                        puts("Raw image saved both sectors and regenerated zero tags from the file");
                        free(raw_tags);
                } else if (!tags) {
                        uint8_t *tag_probe = calloc(1600, TAG_BYTES);
                        CHECK(tag_probe);
                        tag_probe[24 * TAG_BYTES] = 0x5a;
                        sony_request(scratch, 24, data, tag_probe, SONY_WRITE, noErr);
                        sony_request(scratch, 24, data, tag_probe, SONY_READ, noErr);
                        CHECK(disk_img_checkpoint(disk) < 0 && errno == ENOTSUP);
                        CHECK(disk_img_status(disk).dirty_sides == 1);
                        set_expected_checksums(expected, size, 0);
                        CHECK(read_file(writable_path, after, TAGGED_DISKCOPY_SIZE) == size);
                        CHECK(!memcmp(after, expected, size));
                        sony_request(scratch, 24, data, NULL, SONY_WRITE, noErr);
                        puts("Tagless DiskCopy retained the nonzero tag in RAM and refused to save it");
                        free(tag_probe);
                }
        }
        sony_control(scratch, 8, 0, NULL);
        CHECK(disk_img_close(disk) == 0);
        free(before);
        set_expected_checksums(expected, size, skip);
        CHECK(read_file(writable_path, after, TAGGED_DISKCOPY_SIZE) == size);
        for (size_t i = 0; i < size; ++i) {
                if (expected[i] != after[i]) {
                        fprintf(stderr, "Image differs at offset %zu: expected %02x, got %02x\n", i, expected[i], after[i]);
                        break;
                }
        }
        CHECK(!memcmp(expected, after, size));
        pid_t child = fork();
        CHECK(child >= 0);
        if (!child) {
                struct disk_img *reopened = disk_img_open(writable_path);
                _Exit(reopened && disk_img_close(reopened) == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
        }
        int status;
        CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS);
        puts("Reopened image matched all expected data, tags and header bytes");
}

int main(int argc, char **argv)
{
        CHECK(argc >= 3 && argc <= 5);
        bool trace = !strcmp(argv[argc - 1], "--trace");
        int options = argc - 3 - trace;
        bool format = options && !strcmp(argv[3], "--format");
        bool writable = options && (format || !strcmp(argv[3], "--write"));
        CHECK(options == 0 || (options == 1 && writable));
        uint8_t *ram = calloc(1, RAM_SIZE);
        uint8_t *rom = malloc(ROM_SIZE);
        uint8_t *expected = malloc(TAGGED_DISKCOPY_SIZE);
        uint8_t *after = malloc(TAGGED_DISKCOPY_SIZE);
        CHECK(ram && rom && expected && after);
        CHECK(read_file(argv[1], rom, ROM_SIZE) == ROM_SIZE);
        CHECK(rom_is_plus_v3(rom));
        size_t image_size = read_file(argv[2], expected, TAGGED_DISKCOPY_SIZE);
        CHECK(image_size == DISK_IMG_SIZE || image_size == DISKCOPY_SIZE || image_size == TAGGED_DISKCOPY_SIZE);
        const uint8_t *data = expected + (image_size == DISK_IMG_SIZE ? 0 : DISKCOPY_HEADER);
        const uint8_t *tags = image_size == TAGGED_DISKCOPY_SIZE ? data + DISK_IMG_SIZE : NULL;
#ifdef UMAC_TEST_LINK_WRAP
        protected_during_boot = writable && image_size == DISKCOPY_SIZE;
#endif
        if (writable)
                make_writable_image(expected, image_size);
        struct disk_img *disk = writable ? disk_img_open_writable(writable_path) : disk_img_open(argv[2]);
        CHECK(disk && umac_init(ram, rom, disk) == 0);
        if (trace && !writable)
                umac_iwm_trace(stderr);

        boot_finder();
        test_display_and_input();
        /* Let Finder leave interrupt service before issuing the first trap. */
        for (unsigned int i = 0; i < 200 && (m68k_get_reg(NULL, M68K_REG_SR) & 0x0700); ++i)
                step();
        CHECK(!(m68k_get_reg(NULL, M68K_REG_SR) & 0x0700));
        /* Finder's stack is narrow on some boot images. Keep the live SP
         * and allocate probe buffers through the guest Memory Manager. The
         * four-byte allocation stub replaces the abandoned Finder frame. */
        unsigned int stack = m68k_get_reg(NULL, M68K_REG_SP);
        const unsigned int scratch_size = TAG_BUFFER_OFFSET + TAG_BYTES;
        CHECK(stack > 0x2000 && stack <= RAM_SIZE - 4);
        m68k_set_reg(M68K_REG_D0, scratch_size);
        run_trap(stack, 0xa11e, 8, NULL); /* _NewPtr. */
        CHECK((int16_t)m68k_get_reg(NULL, M68K_REG_D0) == noErr);
        unsigned int scratch = m68k_get_reg(NULL, M68K_REG_A0);
        CHECK(scratch && scratch <= RAM_SIZE - scratch_size);
        if (writable) {
#ifdef UMAC_TEST_LINK_WRAP
                if (protected_during_boot) {
                        protected_during_boot = false;
                        /* Ask .Sony to recalibrate and sample the new tab
                         * state on its next request, as for a reinserted disk. */
                        RAM_WR8(RAM_RD32(SONY_VARS) + SONY_DRIVE_1 + SONY_DISK_IN_PLACE, 1);
                }
#endif
                if (trace)
                        umac_iwm_trace(stderr);
                test_writable(disk, scratch, expected, after, image_size, format);
                goto check_rom;
        }
        sony_control(scratch, 8, scratch + TAG_BUFFER_OFFSET, NULL);
        /* One sector on each head in each speed zone, then the last block. */
        const unsigned int blocks[] = {0, 12, 384, 395, 736, 746, 1056, 1065, 1344, 1352, 1599};
        for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
                sony_request(scratch, blocks[i], data, tags, SONY_READ, noErr);
        sony_request(scratch, 1599, data, tags, SONY_WRITE, wPrErr);
        sony_control(scratch, 8, 0, NULL);
        puts(".Sony matched 11 sectors and their tags across both heads and all zones and returned wPrErr for a write");

        struct disk_img_status before_reset = disk_img_status(disk);
        CHECK(before_reset.cylinder == 79);
        ram[CUR_AP_NAME] = 0;
        umac_reset();
        struct disk_img_status after_reset = disk_img_status(disk);
        CHECK(after_reset.cylinder == 79 && after_reset.side == before_reset.side);
        CHECK(!after_reset.spindle && !after_reset.ready_at && !disk_img_sense(disk, 7));
        CHECK(!disk_img_sense(disk, 1) && !disk_img_sense(disk, 3));
        boot_finder(); /* The original ROM must recalibrate from cylinder 79. */
        puts("Original ROM rebooted after seeking from cylinder 79");

        CHECK(read_file(argv[2], after, TAGGED_DISKCOPY_SIZE) == image_size);
        CHECK(!memcmp(expected, after, image_size));
        puts("Read-only floppy matches its input bytes");
        CHECK(disk_img_close(disk) == 0);
check_rom:
        CHECK(read_file(argv[1], after, ROM_SIZE) == ROM_SIZE);
        CHECK(!memcmp(rom, after, ROM_SIZE));
        puts("ROM matches its input bytes");
        free(after);
        free(expected);
        free(rom);
        free(ram);
        return 0;
}
