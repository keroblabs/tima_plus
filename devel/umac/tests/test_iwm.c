#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cpu_cb.h"
#include "iwm.h"
#include "umac.h"

#define CHECK(expr) do {                                                      \
        if (!(expr)) {                                                        \
                fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);     \
                exit(EXIT_FAILURE);                                           \
        }                                                                     \
} while (0)

enum {
        PH0L, PH0H, PH1L, PH1H, PH2L, PH2H, PH3L, PH3H,
        MTROFF, MTRON, INTERNAL, EXTERNAL, Q6L, Q6H, Q7L, Q7H,
};
#define SW(n) (IWM_BASE + 0x200 * (n))
#define VIA(n) (0xefe1feu + 0x200 * (n))

static char image_path[] = "/tmp/umac-iwm-test-XXXXXX";
static const unsigned int sector_counts[] = {12, 11, 10, 9, 8};
static const unsigned int track_lengths[] = {74558, 68475, 62237, 55954, 49789};

static void remove_image(void)
{
        unlink(image_path);
}

static uint8_t fixture_byte(unsigned int block, unsigned int offset)
{
        if (offset < 2)
                return block >> (8 * offset);
        return (53 * block + 31 * offset) ^ (offset >> 3) ^ (block >> 2);
}

static void make_image(void)
{
        int fd = mkstemp(image_path);
        CHECK(fd >= 0);
        atexit(remove_image);
        CHECK(ftruncate(fd, 400 * 1024) == 0);
        CHECK(!disk_img_open(image_path) && errno == EINVAL);
        CHECK(ftruncate(fd, DISK_IMG_SIZE + 84 + 19200) == 0);
        CHECK(!disk_img_open(image_path) && errno == EINVAL);
        CHECK(ftruncate(fd, 1440 * 1024) == 0);
        CHECK(!disk_img_open(image_path) && errno == EINVAL);
        CHECK(ftruncate(fd, 0) == 0);

        FILE *image = fdopen(fd, "wb");
        CHECK(image);
        for (unsigned int block = 0; block < 1600; ++block) {
                uint8_t sector[512];
                for (unsigned int i = 0; i < sizeof(sector); ++i)
                        sector[i] = fixture_byte(block, i);
                CHECK(fwrite(sector, 1, sizeof(sector), image) == sizeof(sector));
        }
        CHECK(fclose(image) == 0);
}

static void verify_image(void)
{
        FILE *image = fopen(image_path, "rb");
        CHECK(image);
        for (unsigned int block = 0; block < 1600; ++block) {
                uint8_t sector[512];
                CHECK(fread(sector, 1, sizeof(sector), image) == sizeof(sector));
                for (unsigned int i = 0; i < sizeof(sector); ++i)
                        CHECK(sector[i] == fixture_byte(block, i));
        }
        CHECK(fgetc(image) == EOF && !ferror(image));
        CHECK(fclose(image) == 0);
}

static void select_register(struct iwm *iwm, unsigned int reg, uint64_t t)
{
        iwm_read(iwm, SW(PH0L + ((reg >> 1) & 1)), t);
        iwm_read(iwm, SW(PH1L + ((reg >> 2) & 1)), t);
        iwm_read(iwm, SW(PH2L + (reg >> 3)), t);
        iwm_set_sel(iwm, reg & 1, t);
}

static uint8_t sense(struct iwm *iwm, unsigned int reg, uint64_t t)
{
        select_register(iwm, reg, t);
        iwm_read(iwm, SW(Q6H), t);
        uint8_t value = iwm_read(iwm, SW(Q7L), t);
        iwm_read(iwm, SW(Q6L), t);
        return value;
}

static void command(struct iwm *iwm, unsigned int reg, bool value, uint64_t t)
{
        select_register(iwm, reg | (value ? 8 : 0), t);
        iwm_write(iwm, SW(PH3H), 0, t);
        iwm_read(iwm, SW(PH3L), t);
}

static void set_mode(struct iwm *iwm, uint8_t mode)
{
        iwm_read(iwm, SW(Q6H), iwm->ticks);
        iwm_write(iwm, SW(Q7H), mode, iwm->ticks);
        CHECK((iwm_read(iwm, SW(Q7L), iwm->ticks) & 0x3f) == (mode & 0x1f));
        iwm_read(iwm, SW(Q6L), iwm->ticks);
}

static void test_empty_drive_and_bus(void)
{
        uint8_t *ram = calloc(1, RAM_SIZE);
        uint8_t *rom = calloc(1, ROM_SIZE);
        struct disk_img *disk = disk_img_open(NULL);
        CHECK(ram && rom && disk && umac_init(ram, rom, disk) == 0);

        CHECK(cpu_read_byte(SW(Q6L)) == 0xff);
        CHECK(cpu_read_byte(SW(Q6H)) == 0x80);
        cpu_write_word(SW(Q7H) - 1, 0xaa1f);
        CHECK(cpu_read_word(SW(Q7L) - 1) == 0xff9f);
        CHECK(cpu_read_byte(SW(MTRON) - 1) == 0xff); /* Upper lane leaves enable off. */
        CHECK(cpu_read_byte(SW(Q7L)) == 0x9f);
        CHECK(cpu_read_byte(SW(MTRON)) == 0x3f);
        cpu_read_byte(SW(PH0H));
        cpu_read_byte(SW(PH1H));
        CHECK(cpu_read_byte(SW(PH2H)) == 0x3f); /* Drive exists. */
        cpu_read_byte(SW(PH0L));
        cpu_read_byte(SW(PH1L));
        cpu_read_byte(SW(PH2L));
        cpu_write_byte(VIA(15), 0x30);
        CHECK(cpu_read_byte(SW(Q7L)) == 0x3f); /* PA5 is still an input. */
        cpu_write_byte(VIA(3), 0x20);
        CHECK(cpu_read_byte(SW(Q7L)) == 0xbf); /* /CSTIN on an installed empty drive. */
        CHECK(cpu_read_byte(VIA(15)) == 0xa0); /* SCC idle and SEL high. */
        cpu_read_byte(SW(PH0H));
        cpu_read_byte(SW(PH1H));
        CHECK(cpu_read_byte(SW(PH2H)) == 0xbf); /* New interface. */
        CHECK(cpu_read_byte(SW(EXTERNAL)) == 0xbf);
        cpu_write_byte(VIA(15), 0x10);
        CHECK(cpu_read_byte(SW(Q7L)) == 0xbf); /* External drive absent. */
        CHECK(cpu_read_byte(SW(INTERNAL)) == 0x3f);
        umac_reset();
        CHECK(cpu_read_byte(SW(Q6H)) == 0x80);
        disk_img_close(disk);
        free(rom);
        free(ram);
}

static void test_drive(struct disk_img *disk)
{
        struct iwm iwm;
        uint64_t t = 0;
        iwm_reset(&iwm, disk, t);
        set_mode(&iwm, 0xff);
        iwm_read(&iwm, SW(MTRON), t);
        CHECK(sense(&iwm, 1, t) == 0x3f);
        CHECK(sense(&iwm, 3, t) == 0x3f); /* /WRPROT low. */
        CHECK(sense(&iwm, 4, t) == 0xbf); /* Enable alone does not start the motor. */
        CHECK(sense(&iwm, 5, t) == 0x3f);
        CHECK(sense(&iwm, 12, t) == 0xbf);
        CHECK(sense(&iwm, 13, t) == 0xbf);

        select_register(&iwm, 4, t);
        CHECK(!disk_img_status(disk).spindle);
        iwm_read(&iwm, SW(EXTERNAL), t);
        command(&iwm, 4, false, t);
        CHECK(!disk_img_status(disk).spindle);
        iwm_read(&iwm, SW(INTERNAL), t);
        command(&iwm, 4, false, t);
        CHECK(sense(&iwm, 4, t) == 0x3f && sense(&iwm, 13, t) == 0xbf);
        t = disk_img_status(disk).ready_at;
        CHECK(t > 0 && sense(&iwm, 13, t - 1) == 0xbf);
        CHECK(sense(&iwm, 13, t) == 0x3f);

        unsigned int half_tach = track_lengths[0] * DISK_IMG_BIT_TICKS / 120;
        uint8_t tach = sense(&iwm, 7, t);
        CHECK(sense(&iwm, 7, t) == tach);
        CHECK(sense(&iwm, 7, t += half_tach) == (tach ^ 0x80));
        CHECK(sense(&iwm, 7, t += half_tach) == tach);

        select_register(&iwm, 2, t);
        CHECK(disk_img_status(disk).step_done_at == 0);
        iwm_read(&iwm, SW(PH3H), t);
        uint64_t done = disk_img_status(disk).step_done_at;
        CHECK(done > t);
        iwm_read(&iwm, SW(PH3H), t + 1);
        iwm_read(&iwm, SW(PH3L), t + 2);
        command(&iwm, 2, false, t + 3); /* Overlapping requests do not queue. */
        CHECK(sense(&iwm, 2, done - 1) == 0x3f);
        CHECK(disk_img_status(disk).cylinder == 0);
        CHECK(sense(&iwm, 2, done) == 0xbf);
        CHECK(sense(&iwm, 5, done) == 0xbf && disk_img_status(disk).cylinder == 1);
        t = done;

        for (unsigned int cylinder = 2; cylinder <= 79; ++cylinder) {
                command(&iwm, 2, false, t);
                t = disk_img_status(disk).step_done_at;
                iwm_advance(&iwm, t);
                CHECK(disk_img_status(disk).cylinder == cylinder);
        }
        command(&iwm, 2, false, t);
        t = disk_img_status(disk).step_done_at;
        iwm_advance(&iwm, t);
        CHECK(disk_img_status(disk).cylinder == 79);
        command(&iwm, 0, true, t);
        CHECK(sense(&iwm, 0, t) == 0xbf && disk_img_status(disk).cylinder == 79);
        for (int cylinder = 78; cylinder >= 0; --cylinder) {
                command(&iwm, 2, false, t);
                t = disk_img_status(disk).step_done_at;
                iwm_advance(&iwm, t);
                CHECK(disk_img_status(disk).cylinder == (unsigned int)cylinder);
        }
        command(&iwm, 2, false, t);
        t = disk_img_status(disk).step_done_at;
        CHECK(sense(&iwm, 5, t) == 0x3f && disk_img_status(disk).cylinder == 0);

        select_register(&iwm, 9, t);
        CHECK(disk_img_status(disk).side == 1);
        sense(&iwm, 12, t);
        CHECK(disk_img_status(disk).side == 1 && disk_img_status(disk).cylinder == 0);
        select_register(&iwm, 8, t);
        CHECK(disk_img_status(disk).side == 0);
        command(&iwm, 4, true, t);
        CHECK(!disk_img_status(disk).spindle);
        iwm_read(&iwm, SW(MTROFF), t);
        CHECK(!iwm_enabled(&iwm));

        set_mode(&iwm, 0x1b);
        select_register(&iwm, 14, t);
        iwm_read(&iwm, SW(MTRON), t);
        iwm_read(&iwm, SW(MTROFF), t);
        CHECK(iwm_enabled(&iwm));
        CHECK(iwm_read(&iwm, SW(Q6H), t + (1u << 23) - 1) == 0x3b);
        CHECK(iwm_read(&iwm, SW(Q6H), t += 1u << 23) == 0x9b);
        iwm_reset(&iwm, disk, t);
        set_mode(&iwm, 0x1f);
        iwm_read(&iwm, SW(MTRON), t);
        CHECK(sense(&iwm, 6, t) == 0xbf && sense(&iwm, 1, t) == 0x3f);
        command(&iwm, 6, true, t);
        CHECK(sense(&iwm, 1, t) == 0xbf && sense(&iwm, 14, t) == 0x3f);
}

static void test_latch(struct disk_img *disk)
{
        struct iwm iwm;
        iwm_reset(&iwm, disk, 0);
        set_mode(&iwm, 0x1f);
        iwm_read(&iwm, SW(MTRON), 0);
        command(&iwm, 4, false, 0);

        uint64_t index = track_lengths[0] * DISK_IMG_BIT_TICKS;
        select_register(&iwm, 8, index);
        CHECK(iwm_read(&iwm, SW(Q6L), index + 15) == 0 && iwm.shift == 0);
        CHECK(iwm_read(&iwm, SW(Q6L), index + 16) == 0 && iwm.shift == 1);
        CHECK(!(iwm_read(&iwm, SW(Q6L), index + 127) & 0x80));
        CHECK(iwm_read(&iwm, SW(Q6L), index + 128) == 0xff);
        CHECK(iwm_read(&iwm, SW(Q6L), index + 141) == 0xff);
        CHECK(iwm_read(&iwm, SW(Q6L), index + 142) == 0x7f);

        const unsigned int sector_bits = 5832; /* Address through data trail. */
        uint64_t mark = index + (track_lengths[0] / 12 - sector_bits + 8) * DISK_IMG_BIT_TICKS;
        CHECK(iwm_read(&iwm, SW(Q6L), mark) == 0xd5);
        CHECK(iwm_read(&iwm, SW(Q6L), mark + 256) == 0x96); /* Missed $AA was replaced. */
        mark += index;
        CHECK(iwm_read(&iwm, SW(Q6L), mark + 123) == 0xd5);
        /* The clear scheduled for $D5 cannot discard the incoming $AA. */
        CHECK(iwm_read(&iwm, SW(Q6L), mark + 138) == 0xaa);
        CHECK(iwm_read(&iwm, SW(Q6L), mark + 152) == 0x2a);

        uint64_t write_at = mark + 160;
        struct disk_img_span before = disk_img_advance(disk, write_at, true);
        CHECK(before.count == 1);
        size_t bytes = (before.length + 7) / 8;
        uint8_t *saved_track = malloc(bytes);
        CHECK(saved_track);
        memcpy(saved_track, before.bits, bytes);
        iwm_read(&iwm, SW(Q6H), write_at);
        iwm_write(&iwm, SW(Q7H), 0x55, write_at);
        CHECK(iwm_read(&iwm, SW(Q6L), write_at) == 0x3f);
        CHECK(iwm_read(&iwm, SW(Q7L), write_at) == 0);
        struct disk_img_span after = disk_img_advance(disk, write_at + 16, true);
        CHECK(after.length == before.length && !memcmp(saved_track, after.bits, bytes));
        free(saved_track);
        CHECK(iwm_read(&iwm, SW(Q6L), mark + 100 * index) == 0xd5);
        verify_image();
}

static unsigned int read_bits(const struct disk_img_span *span, unsigned int position, unsigned int count)
{
        unsigned int value = 0;
        for (unsigned int i = 0; i < count; ++i)
                value = (value << 1) | disk_img_bit(span, (position + i) % span->length);
        return value;
}

static unsigned int ungcr(const struct disk_img_span *span, unsigned int *position)
{
        static const uint8_t code[] = {
                0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
                0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
                0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
                0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
                0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
                0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
                0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
                0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
        };
        unsigned int value = read_bits(span, *position, 8);
        const uint8_t *symbol = memchr(code, value, sizeof(code));
        CHECK(symbol);
        *position += 8;
        return symbol - code;
}

static void verify_payload(const struct disk_img_span *span, unsigned int *pos, unsigned int block)
{
        /* Decode as in Apple's SWIM reference, using full-width sums so
         * carry propagation and the partial final group are checked. */
        unsigned int a = 0, b = 0, c = 0;
        for (unsigned int i = 0; i < 524; i += 3) {
                unsigned int high = ungcr(span, pos);
                unsigned int carry = c >> 7;
                c = ((c << 1) & 0xff) | carry;

                unsigned int byte = ((high & 0x30) << 2 | ungcr(span, pos)) ^ c;
                CHECK(byte == (i < 12 ? 0 : fixture_byte(block, i - 12)));
                unsigned int sum = a + byte + carry;
                a = sum & 0xff;
                byte = ((high & 0x0c) << 4 | ungcr(span, pos)) ^ a;
                CHECK(byte == (i < 12 ? 0 : fixture_byte(block, i + 1 - 12)));
                sum = b + byte + (sum >> 8);
                b = sum & 0xff;
                if (i + 2 < 524) {
                        byte = ((high & 3) << 6 | ungcr(span, pos)) ^ b;
                        CHECK(byte == (i < 12 ? 0 : fixture_byte(block, i + 2 - 12)));
                        c = (c + byte + (sum >> 8)) & 0xff;
                } else {
                        CHECK((high & 3) == 0);
                }
        }
        CHECK(ungcr(span, pos) == ((a >> 6) << 4 | (b >> 6) << 2 | c >> 6));
        CHECK(ungcr(span, pos) == (a & 63));
        CHECK(ungcr(span, pos) == (b & 63));
        CHECK(ungcr(span, pos) == (c & 63));
}

static void verify_track(const struct disk_img_span *span, unsigned int track, unsigned int side,
                         unsigned int first_block)
{
        unsigned int sectors = sector_counts[track / 16];
        CHECK(span->length == track_lengths[track / 16]);
        for (unsigned int slot = 0; slot < sectors; ++slot) {
                unsigned int pos = (slot + 1) * span->length / sectors - 5832;
                CHECK(read_bits(span, pos - 20, 20) == 0xff3fc);
                CHECK(read_bits(span, pos, 24) == 0xd5aa96);
                pos += 24;
                unsigned int sector = (slot & 1) ? (sectors + slot) / 2 : slot / 2;
                unsigned int side_track = (side ? 32 : 0) + track / 64;
                CHECK(ungcr(span, &pos) == (track & 63));
                CHECK(ungcr(span, &pos) == sector);
                CHECK(ungcr(span, &pos) == side_track);
                CHECK(ungcr(span, &pos) == 0x22);
                CHECK(ungcr(span, &pos) == ((track & 63) ^ sector ^ side_track ^ 0x22));
                CHECK(read_bits(span, pos, 32) == 0xdeaaffff);
                CHECK(read_bits(span, pos + 32, 24) == 0xff3fcf);
                CHECK(read_bits(span, pos + 56, 24) == 0xf3fcff);
                pos += 80;
                CHECK(read_bits(span, pos, 24) == 0xd5aaad);
                pos += 24;
                CHECK(ungcr(span, &pos) == sector);
                verify_payload(span, &pos, first_block + sector);
                CHECK(read_bits(span, pos, 32) == 0xdeaaffff);
                CHECK(pos + 32 == (slot + 1) * span->length / sectors);
        }
}

static void test_encoded_disk(struct disk_img *disk)
{
        disk_img_command(disk, 4, false);
        uint64_t t = disk_img_status(disk).ready_at;
        disk_img_advance(disk, t, false);
        unsigned int block = 0;
        for (unsigned int track = 0; track < 80; ++track) {
                unsigned int length = track_lengths[track / 16];
                for (unsigned int side = 0; side < 2; ++side) {
                        disk_img_select_head(disk, side);
                        unsigned int before = disk_img_status(disk).bit_position;
                        struct disk_img_span span = disk_img_advance(disk, t += length * DISK_IMG_BIT_TICKS, true);
                        CHECK(span.count == length && span.first == before);
                        CHECK(disk_img_status(disk).bit_position == before);
                        verify_track(&span, track, side, block);
                        block += sector_counts[track / 16];
                }
                disk_img_command(disk, 2, false);
                t = disk_img_status(disk).step_done_at;
                disk_img_advance(disk, t, false);
        }
        CHECK(block == 1600);
}

static void test_via_timers(void)
{
        via_init(NULL);
        via_write(VIA(14), 0xe0); /* Enable T1 and T2 IRQs. */
        via_write(VIA(8), 4);
        via_write(VIA(9), 0);
        CHECK(via_next_timer_irq() == 60);
        via_advance(39);
        CHECK(via_read(VIA(8)) == 2 && via_read(VIA(13)) == 0);
        via_advance(59);
        CHECK(via_read(VIA(8)) == 0 && via_read(VIA(13)) == 0);
        via_advance(60);
        CHECK(via_read(VIA(13)) == 0xa0);
        CHECK(via_read(VIA(8)) == 0xff && via_read(VIA(13)) == 0);
        via_advance(60 + 65536 * 10);
        CHECK(via_read(VIA(13)) == 0); /* T2 is one-shot even after wrapping. */

        via_init(NULL);
        via_write(VIA(14), 0xc0);
        via_write(VIA(11), 0x40); /* T1 continuous. */
        via_write(VIA(4), 3);
        via_write(VIA(5), 0);
        CHECK(via_next_timer_irq() == 50);
        via_advance(50);
        CHECK(via_read(VIA(13)) == 0xc0 && via_read(VIA(4)) == 0xff);
        CHECK(via_next_timer_irq() == 100 && via_read(VIA(13)) == 0);
        via_advance(100);
        CHECK(via_read(VIA(13)) == 0xc0);
}

int main(int argc, char **argv)
{
        CHECK(argc == 2);
        make_image();
        struct disk_img *disk = disk_img_open(image_path);
        CHECK(disk);
        if (!strcmp(argv[1], "controller")) {
                test_empty_drive_and_bus();
                test_via_timers();
                test_drive(disk);
                disk_img_close(disk);
                disk = disk_img_open(image_path);
                CHECK(disk);
                test_latch(disk);
        } else {
                CHECK(!strcmp(argv[1], "disk"));
                test_encoded_disk(disk);
        }
        disk_img_close(disk);
        return 0;
}
