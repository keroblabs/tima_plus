#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
};

static uint64_t next_vbl = MAC_FRAME_TICKS;
static uint64_t next_second = MAC_C8M_HZ;

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

static void run_trap(unsigned int code, uint16_t trap)
{
        RAM_WR16(code, trap);
        RAM_WR16(code + 2, 0x60fe); /* BRA.S to self. */
        m68k_set_reg(M68K_REG_PC, code);
        uint64_t deadline = umac_ticks() + 8 * MAC_C8M_HZ;
        do {
                step();
        } while (umac_ticks() < deadline && m68k_get_reg(NULL, M68K_REG_PC) != code + 2);

        CHECK(umac_ticks() < deadline);
}

static void sony_set_tag_buffer(unsigned int scratch, unsigned int buffer)
{
        unsigned int pb = scratch + PB_OFFSET;
        memset(ram_get_base() + pb, 0, 64);
        RAM_WR16(pb + ioVRefNum, 1);
        RAM_WR16(pb + ioRefNum, -5);
        RAM_WR16(pb + csCode, 8); /* SetTagBuffer. */
        RAM_WR32(pb + csParam, buffer);
        m68k_set_reg(M68K_REG_A0, pb);
        run_trap(scratch, 0xa004); /* _Control. */
        CHECK((int16_t)RAM_RD16(pb + ioResult) == noErr);
}

static void sony_request(unsigned int scratch, unsigned int block, const uint8_t *data,
                         const uint8_t *tags, bool write)
{
        static const uint8_t empty_tag[TAG_BYTES] = {0};
        unsigned int pb = scratch + PB_OFFSET;
        unsigned int buffer = scratch + SECTOR_BUFFER_OFFSET;
        uint8_t *ram = ram_get_base();
        memset(ram + pb, 0, 64);
        memset(ram + buffer, 0xa5, SECTOR_BYTES + TAG_BYTES);
        RAM_WR16(pb + ioVRefNum, 1);
        RAM_WR16(pb + ioRefNum, -5);
        RAM_WR32(pb + ioBuffer, buffer);
        RAM_WR32(pb + ioReqCount, SECTOR_BYTES);
        RAM_WR16(pb + ioPosMode, fsFromStart);
        RAM_WR32(pb + ioPosOffset, block * SECTOR_BYTES);
        m68k_set_reg(M68K_REG_A0, pb);
        run_trap(scratch, write ? 0xa003 : 0xa002); /* _Write or _Read. */

        int result = (int16_t)RAM_RD16(pb + ioResult);
        if (write) {
                CHECK(result == wPrErr && RAM_RD32(pb + ioActCount) == 0);
        } else {
                if (result)
                        fprintf(stderr, ".Sony returned %d for block %u\n", result, block);
                CHECK(result == noErr && RAM_RD32(pb + ioActCount) == SECTOR_BYTES);
                CHECK(!memcmp(ram + buffer, data + block * SECTOR_BYTES, SECTOR_BYTES));
                const uint8_t *tag = tags ? tags + block * TAG_BYTES : empty_tag;
                CHECK(!memcmp(ram + scratch + TAG_BUFFER_OFFSET, tag, TAG_BYTES));
        }
}

int main(int argc, char **argv)
{
        CHECK(argc == 3 || (argc == 4 && !strcmp(argv[3], "--trace")));
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
        struct disk_img *disk = disk_img_open(argv[2]);
        CHECK(disk && umac_init(ram, rom, disk) == 0);
        if (argc == 4)
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
        run_trap(stack, 0xa11e); /* _NewPtr. */
        CHECK((int16_t)m68k_get_reg(NULL, M68K_REG_D0) == noErr);
        unsigned int scratch = m68k_get_reg(NULL, M68K_REG_A0);
        CHECK(scratch && scratch <= RAM_SIZE - scratch_size);
        sony_set_tag_buffer(scratch, scratch + TAG_BUFFER_OFFSET);
        /* One sector on each head in each speed zone, then the last block. */
        const unsigned int blocks[] = {0, 12, 384, 395, 736, 746, 1056, 1065, 1344, 1352, 1599};
        for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
                sony_request(scratch, blocks[i], data, tags, false);
        sony_request(scratch, 1599, data, tags, true);
        sony_set_tag_buffer(scratch, 0);
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

        CHECK(read_file(argv[1], after, ROM_SIZE) == ROM_SIZE);
        CHECK(!memcmp(rom, after, ROM_SIZE));
        CHECK(read_file(argv[2], after, TAGGED_DISKCOPY_SIZE) == image_size);
        CHECK(!memcmp(expected, after, image_size));
        puts("ROM and floppy contents match their input bytes");
        disk_img_close(disk);
        free(after);
        free(expected);
        free(rom);
        free(ram);
        return 0;
}
