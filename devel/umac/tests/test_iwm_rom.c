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
        SECTOR_BYTES = 512,
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

static void read_file(const char *path, uint8_t *bytes, size_t size)
{
        FILE *file = fopen(path, "rb");
        CHECK(file);
        CHECK(fread(bytes, 1, size, file) == size && fgetc(file) == EOF && !ferror(file));
        CHECK(fclose(file) == 0);
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

/* Execute a synchronous Device Manager trap against the ROM-installed
 * .Sony driver. The code, parameter block and buffer occupy guest stack
 * scratch space above the test stack pointer. */
static void sony_request(unsigned int scratch, unsigned int block, const uint8_t *expected, bool write)
{
        unsigned int pb = scratch + 16;
        unsigned int buffer = scratch + 128;
        uint8_t *ram = ram_get_base();
        memset(ram + pb, 0, 64);
        memset(ram + buffer, 0xa5, SECTOR_BYTES);
        RAM_WR16(pb + ioVRefNum, 1);
        RAM_WR16(pb + ioRefNum, -5);
        RAM_WR32(pb + ioBuffer, buffer);
        RAM_WR32(pb + ioReqCount, SECTOR_BYTES);
        RAM_WR16(pb + ioPosMode, 1); /* fsFromStart. */
        RAM_WR32(pb + ioPosOffset, block * SECTOR_BYTES);
        RAM_WR16(scratch, write ? 0xa003 : 0xa002); /* _Write or _Read. */
        RAM_WR16(scratch + 2, 0x60fe); /* BRA.S to self. */

        m68k_set_reg(M68K_REG_SP, scratch);
        m68k_set_reg(M68K_REG_A0, pb);
        m68k_set_reg(M68K_REG_PC, scratch);
        uint64_t deadline = umac_ticks() + 8 * MAC_C8M_HZ;
        do {
                step();
        } while (umac_ticks() < deadline && m68k_get_reg(NULL, M68K_REG_PC) != scratch + 2);

        CHECK(umac_ticks() < deadline);
        int result = (int16_t)RAM_RD16(pb + ioResult);
        if (write) {
                CHECK(result == wPrErr && RAM_RD32(pb + ioActCount) == 0);
        } else {
                if (result)
                        fprintf(stderr, ".Sony returned %d for block %u\n", result, block);
                CHECK(result == noErr && RAM_RD32(pb + ioActCount) == SECTOR_BYTES);
                CHECK(!memcmp(ram + buffer, expected + block * SECTOR_BYTES, SECTOR_BYTES));
        }
}

int main(int argc, char **argv)
{
        CHECK(argc == 3 || (argc == 4 && !strcmp(argv[3], "--trace")));
        uint8_t *ram = calloc(1, RAM_SIZE);
        uint8_t *rom = malloc(ROM_SIZE);
        uint8_t *expected = malloc(DISK_IMG_SIZE);
        uint8_t *after = malloc(DISK_IMG_SIZE);
        CHECK(ram && rom && expected && after);
        read_file(argv[1], rom, ROM_SIZE);
        CHECK(rom_is_plus_v3(rom));
        read_file(argv[2], expected, DISK_IMG_SIZE);
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
        unsigned int stack = m68k_get_reg(NULL, M68K_REG_SP);
        CHECK(stack > 0x2000 && stack < RAM_SIZE);
        unsigned int scratch = (stack - 1024) & ~1u;
        /* One sector on each head in each speed zone, then the last block. */
        const unsigned int blocks[] = {0, 12, 384, 395, 736, 746, 1056, 1065, 1344, 1352, 1599};
        for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i)
                sony_request(scratch, blocks[i], expected, false);
        sony_request(scratch, 1599, expected, true);
        puts(".Sony matched 11 sectors across both heads and all zones and returned wPrErr for a write");

        CHECK(disk_img_status(disk).cylinder == 79);
        ram[CUR_AP_NAME] = 0;
        umac_reset();
        boot_finder(); /* The original ROM must recalibrate from cylinder 79. */
        puts("Original ROM rebooted after seeking from cylinder 79");

        read_file(argv[1], after, ROM_SIZE);
        CHECK(!memcmp(rom, after, ROM_SIZE));
        read_file(argv[2], after, DISK_IMG_SIZE);
        CHECK(!memcmp(expected, after, DISK_IMG_SIZE));
        puts("ROM and floppy contents match their input bytes");
        disk_img_close(disk);
        free(after);
        free(expected);
        free(rom);
        free(ram);
        return 0;
}
