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

static uint8_t fixture_tag(unsigned int block, unsigned int offset)
{
        return (block >> (8 * (offset % 2))) + 17 * offset + 0x5b;
}

static uint8_t fixture_payload(unsigned int block, unsigned int offset, bool tagged)
{
        if (offset < 12)
                return tagged ? fixture_tag(block, offset) : 0;
        return fixture_byte(block, offset - 12);
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
        iwm_init(&iwm, disk, t);
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
        iwm_reset(&iwm, t);
        set_mode(&iwm, 0x1f);
        iwm_read(&iwm, SW(MTRON), t);
        CHECK(sense(&iwm, 6, t) == 0xbf && sense(&iwm, 1, t) == 0x3f);
        command(&iwm, 6, true, t);
        CHECK(sense(&iwm, 1, t) == 0xbf && sense(&iwm, 14, t) == 0x3f);
}

static const uint64_t motor_off_delay = UINT64_C(1) << 23;

static void trace_motor(const char *event, const struct iwm *iwm)
{
        struct disk_img_status status = disk_img_status(iwm->internal);
        bool external = (iwm->switches & IWM_EXTERNAL) != 0;
        uint8_t enables = iwm_drive_enables(iwm);
        printf("%s t=%" PRIu64 " motor=%u mode=%02x off_at=%" PRIu64
               " drive=%u enbl=%u/%u spindle=%u ready_n=%u tach=%u trk=%u side=%u media=%u bit=%u\n",
               event, iwm->ticks, !!(iwm->switches & IWM_MOTOR), iwm->mode, iwm->motor_off_at,
               external, !(enables & IWM_ENBL1), !(enables & IWM_ENBL2), status.spindle,
               disk_img_sense(iwm->internal, 13), disk_img_sense(iwm->internal, 7),
               status.cylinder, status.side, !disk_img_sense(iwm->internal, 1), status.bit_position);
}

static struct disk_img *start_motor_test(struct iwm *iwm, uint8_t mode)
{
        struct disk_img *disk = disk_img_open(image_path);
        CHECK(disk);
        iwm_init(iwm, disk, 0);
        set_mode(iwm, mode);
        iwm_read(iwm, SW(MTRON), 0);
        command(iwm, 4, false, 0);
        select_register(iwm, 8, 0);
        CHECK(disk_img_status(disk).spindle);
        return disk;
}

static void check_motor_stopped(struct disk_img *disk)
{
        struct disk_img_status status = disk_img_status(disk);
        CHECK(!status.spindle && !status.ready_at);
        CHECK(disk_img_sense(disk, 13) && !disk_img_sense(disk, 7));
        CHECK(!disk_img_sense(disk, 1) && !disk_img_sense(disk, 3));
}

static void test_immediate_motor_off(void)
{
        struct iwm iwm;
        struct disk_img *disk = start_motor_test(&iwm, 0x1f);
        uint64_t rev = track_lengths[0] * DISK_IMG_BIT_TICKS;
        uint64_t t = rev + (rev + 119) / 120;
        uint64_t spin_up = disk_img_status(disk).ready_at;
        CHECK((iwm_read(&iwm, SW(Q6L), t) & 0x80) && iwm.clear_at > t);
        CHECK(disk_img_sense(disk, 7) && !disk_img_sense(disk, 13));
        unsigned int bit = disk_img_status(disk).bit_position;
        trace_motor("R6-T1 running", &iwm);
        CHECK(iwm_read(&iwm, SW(MTROFF), t) == 0xff && iwm_drive_enables(&iwm) == 0);
        trace_motor("R6-T1 stopped", &iwm);
        check_motor_stopped(disk);
        CHECK(!iwm.shift && !iwm.latch && !iwm.clear_at);

        iwm_read(&iwm, SW(MTRON), t += rev);
        CHECK(iwm_read(&iwm, SW(Q6L), t += rev) == 0);
        CHECK(disk_img_status(disk).bit_position == bit);
        CHECK(sense(&iwm, 4, t) == 0xbf && sense(&iwm, 13, t) == 0xbf);
        trace_motor("R6-T1 reselected", &iwm);
        command(&iwm, 4, false, t);
        CHECK(disk_img_status(disk).ready_at == t + spin_up);
        CHECK(sense(&iwm, 13, t + spin_up - 1) == 0xbf);
        CHECK(sense(&iwm, 13, t + spin_up) == 0x3f);
        disk_img_close(disk);
}

static void test_delayed_motor_off(bool jump)
{
        struct iwm iwm;
        struct disk_img *disk = start_motor_test(&iwm, 0x1b);
        uint64_t rev = track_lengths[0] * DISK_IMG_BIT_TICKS;
        uint64_t deadline = rev + motor_off_delay;
        iwm_read(&iwm, SW(MTROFF), rev);
        CHECK(iwm.motor_off_at == deadline && iwm_drive_enables(&iwm) == IWM_ENBL1);
        CHECK(disk_img_status(disk).spindle && !disk_img_sense(disk, 13));
        trace_motor("R6-T2 pending", &iwm);
        if (!jump) {
                iwm_read(&iwm, SW(MTROFF), deadline - 1);
                CHECK(iwm.motor_off_at == deadline && iwm_drive_enables(&iwm) == IWM_ENBL1);
                CHECK(disk_img_status(disk).spindle && !disk_img_sense(disk, 13));
                trace_motor("R6-T2 last enabled tick", &iwm);
        }

        iwm_advance(&iwm, deadline + (jump ? rev + 256 : 0));
        trace_motor(jump ? "R6-T2 after jump" : "R6-T2 at deadline", &iwm);
        CHECK(!iwm.motor_off_at && iwm_drive_enables(&iwm) == 0);
        check_motor_stopped(disk);
        CHECK(!iwm.shift && !iwm.latch && !iwm.clear_at);
        CHECK(disk_img_status(disk).bit_position == (deadline % rev) / DISK_IMG_BIT_TICKS);
        disk_img_close(disk);
}

static void test_motor_off_cancellation(void)
{
        struct iwm iwm;
        struct disk_img *disk = start_motor_test(&iwm, 0x1b);
        uint64_t rev = track_lengths[0] * DISK_IMG_BIT_TICKS;
        uint64_t ready_at = disk_img_status(disk).ready_at;
        uint64_t deadline = rev + motor_off_delay;
        iwm_read(&iwm, SW(MTROFF), rev);
        iwm_read(&iwm, SW(MTRON), deadline - 1);
        uint64_t t = deadline + 256;
        iwm_advance(&iwm, t);
        CHECK(iwm_enabled(&iwm) && !iwm.motor_off_at);
        struct disk_img_status status = disk_img_status(disk);
        CHECK(status.spindle && status.ready_at == ready_at);
        CHECK(status.bit_position == (t % rev) / DISK_IMG_BIT_TICKS);
        trace_motor("R6-T3 canceled", &iwm);

        iwm_read(&iwm, SW(MTROFF), t);
        iwm_read(&iwm, SW(MTRON), t + motor_off_delay);
        CHECK(iwm_enabled(&iwm) && !iwm.motor_off_at);
        check_motor_stopped(disk);
        trace_motor("R6-T3 re-enabled at deadline", &iwm);
        disk_img_close(disk);
}

static void test_motor_drive_select(void)
{
        struct iwm iwm;
        struct disk_img *disk = start_motor_test(&iwm, 0x1b);
        uint64_t t = track_lengths[0] * DISK_IMG_BIT_TICKS;
        iwm_read(&iwm, SW(MTROFF), t);
        iwm_read(&iwm, SW(EXTERNAL), t);
        CHECK(iwm_drive_enables(&iwm) == IWM_ENBL2 && iwm.motor_off_at == t + motor_off_delay);
        trace_motor("R6-T4 external selected", &iwm);
        check_motor_stopped(disk);
        command(&iwm, 4, false, t);
        command(&iwm, 2, false, t);
        command(&iwm, 6, true, t);
        CHECK(sense(&iwm, 14, t) == 0xbb);
        CHECK(!disk_img_status(disk).step_done_at);
        check_motor_stopped(disk);

        iwm_read(&iwm, SW(INTERNAL), t);
        CHECK(iwm_drive_enables(&iwm) == IWM_ENBL1);
        CHECK(sense(&iwm, 4, t) == 0xbb && sense(&iwm, 1, t) == 0x3b);
        trace_motor("R6-T4 internal reselected", &iwm);
        disk_img_close(disk);
}

static void test_motor_reset(void)
{
        struct iwm iwm;
        struct disk_img *disk = start_motor_test(&iwm, 0x1f);
        uint64_t t = disk_img_status(disk).ready_at;
        command(&iwm, 2, false, t);
        uint64_t done = disk_img_status(disk).step_done_at;
        iwm_read(&iwm, SW(MTRON), done - 1);
        iwm_read(&iwm, SW(INTERNAL), done - 1);
        CHECK(disk_img_status(disk).spindle && disk_img_status(disk).step_done_at == done);
        iwm_advance(&iwm, done);
        CHECK(disk_img_status(disk).cylinder == 1);

        command(&iwm, 2, false, done);
        select_register(&iwm, 9, done);
        t = disk_img_status(disk).step_done_at - 1;
        iwm_advance(&iwm, t);
        unsigned int bit = disk_img_status(disk).bit_position;
        trace_motor("R6-T5 before reset", &iwm);
        iwm_reset(&iwm, t);
        trace_motor("R6-T5 after reset", &iwm);
        CHECK(!iwm_enabled(&iwm));
        check_motor_stopped(disk);
        iwm_reset(&iwm, t);
        iwm_advance(&iwm, ++t);
        struct disk_img_status status = disk_img_status(disk);
        CHECK(status.cylinder == 1 && status.side == 1);
        CHECK(status.bit_position == bit && !status.step_done_at);

        set_mode(&iwm, 0x1f);
        iwm_read(&iwm, SW(MTRON), t);
        command(&iwm, 2, false, t);
        done = disk_img_status(disk).step_done_at;
        iwm_read(&iwm, SW(MTROFF), done);
        CHECK(disk_img_status(disk).cylinder == 2 && !disk_img_status(disk).step_done_at);
        verify_image();
        disk_img_close(disk);
}

static void test_latch(struct disk_img *disk)
{
        struct iwm iwm;
        iwm_init(&iwm, disk, 0);
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

static void verify_payload(const struct disk_img_span *span, unsigned int *pos, unsigned int block,
                           bool tagged)
{
        /* Decode as in Apple's SWIM reference, using full-width sums so
         * carry propagation and the partial final group are checked. */
        unsigned int a = 0, b = 0, c = 0;
        for (unsigned int i = 0; i < 524; i += 3) {
                unsigned int high = ungcr(span, pos);
                unsigned int carry = c >> 7;
                c = ((c << 1) & 0xff) | carry;

                unsigned int byte = ((high & 0x30) << 2 | ungcr(span, pos)) ^ c;
                CHECK(byte == fixture_payload(block, i, tagged));
                unsigned int sum = a + byte + carry;
                a = sum & 0xff;
                byte = ((high & 0x0c) << 4 | ungcr(span, pos)) ^ a;
                CHECK(byte == fixture_payload(block, i + 1, tagged));
                sum = b + byte + (sum >> 8);
                b = sum & 0xff;
                if (i + 2 < 524) {
                        byte = ((high & 3) << 6 | ungcr(span, pos)) ^ b;
                        CHECK(byte == fixture_payload(block, i + 2, tagged));
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
                         unsigned int first_block, bool tagged)
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
                verify_payload(span, &pos, first_block + sector, tagged);
                CHECK(read_bits(span, pos, 32) == 0xdeaaffff);
                CHECK(pos + 32 == (slot + 1) * span->length / sectors);
        }
}

static void test_encoded_disk(struct disk_img *disk, bool tagged)
{
        disk_img_set_enabled(disk, true, 0);
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
                        verify_track(&span, track, side, block, tagged);
                        block += sector_counts[track / 16];
                }
                disk_img_command(disk, 2, false);
                t = disk_img_status(disk).step_done_at;
                disk_img_advance(disk, t, false);
        }
        CHECK(block == 1600);
}

static void write_fixture(const uint8_t *bytes, size_t length)
{
        FILE *file = fopen(image_path, "wb");
        CHECK(file);
        CHECK(fwrite(bytes, 1, length, file) == length);
        CHECK(fclose(file) == 0);
}

static void test_diskcopy(void)
{
        const unsigned int data_start = 84;
        const unsigned int tag_start = data_start + DISK_IMG_SIZE;
        const unsigned int image_size = tag_start + 1600 * 12;
        uint8_t *image = calloc(1, image_size);
        CHECK(image);
        int fd = mkstemp(image_path);
        CHECK(fd >= 0);
        atexit(remove_image);
        CHECK(close(fd) == 0);

        image[0] = 7;
        memcpy(image + 1, "Test800", 7);
        WRITE_LONG(image, 64, DISK_IMG_SIZE);
        WRITE_LONG(image, 68, 1600 * 12);
        /* Precomputed checksums for the generated sector and tag patterns. */
        WRITE_LONG(image, 72, 0xffdc66dbu);
        WRITE_LONG(image, 76, 0x0c3c779fu); /* Omits the first tag. */
        image[80] = 1;
        image[81] = 0x22;
        WRITE_WORD(image, 82, 0x0100);
        for (unsigned int block = 0; block < 1600; ++block) {
                for (unsigned int i = 0; i < 512; ++i)
                        image[data_start + 512 * block + i] = fixture_byte(block, i);
                for (unsigned int i = 0; i < 12; ++i)
                        image[tag_start + 12 * block + i] = fixture_tag(block, i);
        }

        write_fixture(image, image_size);
        struct disk_img *disk = disk_img_open(image_path);
        CHECK(disk);
        test_encoded_disk(disk, true);
        disk_img_close(disk);

        const unsigned int header_offsets[] = {0, 67, 71, 80, 81, 82};
        for (size_t i = 0; i < sizeof(header_offsets) / sizeof(header_offsets[0]); ++i) {
                image[header_offsets[i]] ^= 0x80;
                write_fixture(image, image_size);
                CHECK(!disk_img_open(image_path) && errno == EINVAL);
                image[header_offsets[i]] ^= 0x80;
        }
        write_fixture(image, tag_start);
        CHECK(!disk_img_open(image_path) && errno == EINVAL);
        write_fixture(image, image_size - 1);
        CHECK(!disk_img_open(image_path) && errno == EINVAL);

        image[data_start] ^= 1;
        write_fixture(image, image_size);
        CHECK(!disk_img_open(image_path) && errno == EBADMSG);
        image[data_start] ^= 1;
        image[tag_start + 12] ^= 1;
        write_fixture(image, image_size);
        CHECK(!disk_img_open(image_path) && errno == EBADMSG);
        image[tag_start + 12] ^= 1;

        WRITE_LONG(image, 76, 0x0c553d76u); /* Covers all tags. */
        write_fixture(image, image_size);
        disk = disk_img_open(image_path);
        CHECK(disk);
        disk_img_close(disk);

        WRITE_LONG(image, 68, 0);
        WRITE_LONG(image, 76, 0);
        write_fixture(image, tag_start);
        disk = disk_img_open(image_path);
        CHECK(disk);
        test_encoded_disk(disk, false);
        disk_img_close(disk);
        free(image);
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
        if (!strcmp(argv[1], "diskcopy")) {
                test_diskcopy();
                return 0;
        }
        make_image();
        if (!strcmp(argv[1], "motor")) {
                test_immediate_motor_off();
                test_delayed_motor_off(false);
                test_delayed_motor_off(true);
                test_motor_off_cancellation();
                test_motor_drive_select();
                test_motor_reset();
                return 0;
        }
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
                test_encoded_disk(disk, false);
        }
        disk_img_close(disk);
        return 0;
}
