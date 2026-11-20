#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cpu_cb.h"
#include "m68k.h"
#include "scsi_disk.h"
#include "umac.h"

#define CHECK(expr) do {                                                       \
        if (!(expr)) {                                                         \
                fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr);      \
                exit(EXIT_FAILURE);                                            \
        }                                                                      \
} while (0)

enum {
        SCSI = 0x580000,
        DACK = 0x580200,
        DATA = 0, ICR, MODE, TCR, CBS, BSR, INPUT, CLEAR_IRQ,
        BSY = 0x40, REQ = 0x20, SEL = 0x02,
        DRQ = 0x40, IRQ = 0x10, MATCH = 0x08,
};

static unsigned int rd(unsigned int reg)
{
        return cpu_read_byte(SCSI + reg * 16);
}

static void wr(unsigned int reg, unsigned int value)
{
        cpu_write_byte(SCSI + reg * 16 + 1, value);
}

static void expect_phase(enum scsi_phase phase)
{
        if (phase == SCSI_BUS_FREE)
                CHECK((rd(CBS) & (BSY | REQ | SEL)) == 0);
        else
                CHECK((rd(CBS) & 0x7e) == (BSY | REQ | (unsigned int)phase << 2));
}

static void select_target(unsigned int id, bool present)
{
        expect_phase(SCSI_BUS_FREE);
        /* SCSI Manager sequence: arbitrate, assert SEL/BSY, then drop own BSY. */
        wr(DATA, 0x80);
        wr(MODE, 1);
        CHECK((rd(ICR) & 0x60) == 0x40);
        CHECK(rd(DATA) == 0x80);
        wr(ICR, 0x04);
        wr(TCR, 0);
        wr(DATA, 0x80 | (1u << id));
        wr(ICR, 0x0d);
        wr(MODE, 0);
        wr(CBS, 0);
        wr(ICR, 0x05);
        CHECK((rd(ICR) & 0x60) == 0);
        for (int i = 0; i < 10; ++i)
                CHECK((rd(CBS) & (BSY | REQ | SEL)) == (present ? BSY | SEL : SEL));
        wr(ICR, 0);
        expect_phase(present ? SCSI_COMMAND : SCSI_BUS_FREE);
}

static void send_byte(uint8_t data)
{
        CHECK(rd(CBS) & REQ);
        CHECK(rd(BSR) & MATCH);
        wr(DATA, data);
        wr(ICR, 0x11);
        CHECK(!(rd(CBS) & REQ));
        wr(ICR, 0);
}

static unsigned int receive_byte(void)
{
        CHECK(rd(CBS) & REQ);
        CHECK(rd(BSR) & MATCH);
        unsigned int data = rd(DATA);
        wr(ICR, 0x10);
        CHECK(!(rd(CBS) & REQ));
        wr(ICR, 0);
        return data;
}

static void finish(unsigned int status)
{
        expect_phase(SCSI_STATUS);
        wr(TCR, SCSI_STATUS);
        CHECK(receive_byte() == status);
        expect_phase(SCSI_MESSAGE_IN);
        wr(TCR, SCSI_MESSAGE_IN);
        CHECK(receive_byte() == 0);
        expect_phase(SCSI_BUS_FREE);
}

static void command(const uint8_t *cdb, unsigned int length)
{
        select_target(1, true);
        wr(TCR, SCSI_COMMAND);
        for (unsigned int i = 0; i < length; ++i)
                send_byte(cdb[i]);
}

struct fixture {
        uint8_t command[6];
        unsigned int command_pos, data_pos, bus_resets;
};

static void fixture_reset(void *ctx, bool bus_reset)
{
        struct fixture *f = ctx;
        f->command_pos = f->data_pos = 0;
        f->bus_resets += bus_reset;
}

static uint8_t fixture_peek(void *ctx, enum scsi_phase phase)
{
        struct fixture *f = ctx;
        return phase == SCSI_DATA_IN ? 0xc0 + f->data_pos : 0;
}

static enum scsi_phase fixture_transfer(void *ctx, enum scsi_phase phase, uint8_t data)
{
        struct fixture *f = ctx;
        if (phase == SCSI_COMMAND) {
                CHECK(f->command_pos < sizeof(f->command));
                f->command[f->command_pos++] = data;
                return f->command_pos == sizeof(f->command) ? SCSI_DATA_IN : phase;
        }
        if (phase == SCSI_DATA_IN)
                return ++f->data_pos == 8 ? SCSI_STATUS : phase;
        return phase == SCSI_STATUS ? SCSI_MESSAGE_IN : SCSI_BUS_FREE;
}

static void test_controller(void)
{
        expect_phase(SCSI_BUS_FREE);
        CHECK(rd(BSR) == MATCH);
        wr(TCR, 0xff);
        CHECK(rd(TCR) == 0x0f && !(rd(BSR) & MATCH));
        wr(ICR, 0x22);
        CHECK(rd(ICR) == 0x02 && (rd(BSR) & 2));
        wr(ICR, 8);
        wr(DATA, 0x80);
        wr(MODE, 1);
        CHECK(!(rd(ICR) & 0x60));        /* Cannot arbitrate a busy bus. */
        wr(MODE, 0);
        wr(ICR, 0);
        wr(DATA, 0x40);
        wr(MODE, 1);
        CHECK(!(rd(ICR) & 0x60));        /* Not the Mac's arbitration ID. */
        wr(MODE, 0);

        /* Missing target/selection interrupt and normal versus dummy MMIO. */
        wr(CBS, 0x04);
        wr(DATA, 0x84);
        wr(ICR, 5);
        CHECK(rd(BSR) & IRQ);
        CHECK(!(rd(CBS) & BSY));
        CHECK(cpu_read_byte(0x580041) == 0);
        CHECK(cpu_read_byte(0x500040) == 0);
        CHECK(cpu_read_byte(0x580071) == 0 && (rd(BSR) & IRQ));
        CHECK(rd(CLEAR_IRQ) == 0 && !(rd(BSR) & IRQ));
        wr(ICR, 0);

        wr(MODE, 0x06);                 /* Monitor BSY on a free bus. */
        CHECK(rd(MODE) == 0x04 && (rd(BSR) & (IRQ | 4)) == (IRQ | 4));
        cpu_read_word(0x580070);
        CHECK(!(rd(BSR) & (IRQ | 4)));
        wr(MODE, 0);

        struct fixture f = {0};
        struct scsi_target target = { .id = 3, .ctx = &f, .reset = fixture_reset,
                .peek = fixture_peek, .transfer = fixture_transfer };
        struct scsi_target incomplete[] = {target, target, target};
        incomplete[0].peek = NULL;
        incomplete[1].transfer = NULL;
        incomplete[2].reset = NULL;
        for (size_t i = 0; i < sizeof(incomplete) / sizeof(incomplete[0]); ++i) {
                CHECK(ncr5380_attach(&target));
                CHECK(!ncr5380_attach(&incomplete[i]));
                select_target(target.id, false);
        }
        CHECK(ncr5380_attach(&target));
        select_target(1, false);
        select_target(3, true);
        wr(TCR, SCSI_COMMAND);
        wr(DATA, 0x99);
        wr(ICR, 0x10);                  /* An output handshake without DATA is invalid. */
        wr(ICR, 0);
        CHECK(f.command_pos == 0);

        const uint8_t cdb[] = {0x08, 0x00, 0x00, 0x64, 0x01, 0x00};
        for (unsigned int i = 0; i < sizeof(cdb); ++i) {
                expect_phase(SCSI_COMMAND);
                cpu_write_byte(0x580000, cdb[i] ^ 0xff);
                wr(DATA, cdb[i]);
                wr(ICR, 1);
                CHECK(cpu_read_byte(0x5ff180) == cdb[i]);   /* A7/A8/upper mirror, A9 clear. */
                CHECK(cpu_read_byte(0x580100) == cdb[i]);   /* A8 alone is not DACK. */
                CHECK(f.command_pos == i);
                wr(ICR, 0x11);
                wr(ICR, 0x11);
                wr(DATA, 0xff);
                CHECK(f.command_pos == i && !(rd(CBS) & REQ) && (rd(BSR) & 1));
                wr(ICR, 0);
        }
        CHECK(f.command_pos == sizeof(cdb) && !memcmp(f.command, cdb, sizeof(cdb)));
        expect_phase(SCSI_DATA_IN);

        /* No start, wrong start register and mismatched phase all leave byte 0 pending. */
        wr(CLEAR_IRQ, 0);
        wr(MODE, 2);
        wr(INPUT, 0);
        CHECK(!(rd(BSR) & DRQ) && (rd(BSR) & IRQ));
        wr(CLEAR_IRQ, 0);
        CHECK(cpu_read_byte(DACK) == 0 && f.data_pos == 0);
        wr(TCR, SCSI_DATA_IN);
        CHECK(rd(INPUT) == 0xc0 && (rd(BSR) & (DRQ | MATCH)) == (DRQ | MATCH));
        CHECK(cpu_read_byte(DACK + 1) == 0 && cpu_read_word(DACK + 1) == 0);
        CHECK(cpu_read_byte(0x580100) == 0xc0 && f.data_pos == 0);
        wr(ICR, 0x10);
        CHECK(!(rd(BSR) & DRQ) && cpu_read_byte(DACK) == 0xc0 && f.data_pos == 0);
        wr(ICR, 0);
        CHECK(cpu_read_word(DACK) == 0xc100 && f.data_pos == 2);
        CHECK(cpu_read_long(DACK) == 0xc200c300 && f.data_pos == 4);
        for (unsigned int i = 4; i < 8; ++i)
                CHECK(cpu_read_byte(0x5ff270) == 0xc0 + i);
        expect_phase(SCSI_STATUS);
        CHECK((rd(BSR) & (DRQ | IRQ | MATCH | 0x80)) == IRQ);
        CHECK(cpu_read_byte(DACK) == 0xc7);
        wr(MODE, 0);
        finish(0);

        select_target(3, true);
        wr(ICR, 0x80);
        CHECK(rd(CBS) == 0x80 && (rd(BSR) & IRQ) && f.bus_resets == 1);
        wr(MODE, 1);
        CHECK(rd(MODE) == 0 && !(rd(ICR) & 0x40));
        wr(ICR, 0);
        (void)rd(CLEAR_IRQ);
        expect_phase(SCSI_BUS_FREE);
        select_target(3, true);
        cpu_pulse_reset();
        CHECK(rd(BSR) == MATCH && f.bus_resets == 1);
        expect_phase(SCSI_BUS_FREE);
        ncr5380_attach(NULL);
}

enum { IMAGE_BLOCKS = 300 };
static char image_path[] = "/tmp/umac-scsi-XXXXXX";

static void remove_image(void)
{
        unlink(image_path);
}

static uint8_t pattern(unsigned int lba, unsigned int byte, bool changed)
{
        return (17 * lba + (lba >> 8) + 31 * byte + (byte >> 8)) ^ (changed ? 0x59 : 0);
}

static int make_image(void)
{
        int fd = mkstemp(image_path);
        CHECK(fd >= 0);
        CHECK(atexit(remove_image) == 0);
        return fd;
}

static void attach_disk(struct scsi_disk *disk, const struct scsi_disk_media *media)
{
        ncr5380_attach(NULL);
        scsi_disk_init(disk, media);
        struct scsi_target target = scsi_disk_target(disk, 1);
        CHECK(ncr5380_attach(&target));
}

static void rw_command(uint8_t opcode, unsigned int lba, uint8_t blocks)
{
        uint8_t cdb[] = {opcode, lba >> 16, lba >> 8, lba, blocks, 0};
        command(cdb, sizeof(cdb));
}

static void read_sense(unsigned int key, unsigned int asc)
{
        const uint8_t sense[] = {0x03, 0, 0, 0, 18, 0};
        command(sense, sizeof(sense));
        expect_phase(SCSI_DATA_IN);
        wr(TCR, SCSI_DATA_IN);
        for (unsigned int i = 0; i < 18; ++i) {
                unsigned int data = receive_byte();
                if (i == 0) CHECK(data == 0x70);
                if (i == 2) CHECK(data == key);
                if (i == 12) CHECK(data == asc);
        }
        finish(0);
}

static void test_disk(void)
{
        int fd = make_image();
        struct scsi_image image;
        CHECK(scsi_image_open(&image, image_path, false) == -1 && errno == EINVAL);
        CHECK(ftruncate(fd, 511) == 0);
        CHECK(scsi_image_open(&image, image_path, false) == -1 && errno == EINVAL);
        CHECK(ftruncate(fd, (off_t)(SCSI_DISK_MAX_BLOCKS + 1) * SCSI_SECTOR_SIZE) == 0);
        CHECK(scsi_image_open(&image, image_path, false) == -1 && errno == EFBIG);
        CHECK(scsi_image_open(&image, "/tmp", false) == -1 && errno == EINVAL);
        CHECK(ftruncate(fd, IMAGE_BLOCKS * SCSI_SECTOR_SIZE) == 0);
        for (unsigned int lba = 0; lba < IMAGE_BLOCKS; ++lba) {
                uint8_t sector[SCSI_SECTOR_SIZE];
                for (unsigned int i = 0; i < sizeof(sector); ++i)
                        sector[i] = pattern(lba, i, false);
                CHECK(pwrite(fd, sector, sizeof(sector), lba * sizeof(sector)) == (ssize_t)sizeof(sector));
        }
        CHECK(close(fd) == 0);
        CHECK(chmod(image_path, 0444) == 0);
        if (geteuid() != 0)
                CHECK(scsi_image_open(&image, image_path, true) == -1 && errno == EACCES);
        CHECK(chmod(image_path, 0600) == 0);
        CHECK(scsi_image_open(&image, image_path, true) == 0);
        struct scsi_disk disk;
        attach_disk(&disk, &image.media);

        const uint8_t tur[] = {0, 0, 0, 0, 0, 0};
        command(tur, sizeof(tur));
        finish(0);

        /* Abort the last byte before dropping ACK: not a single sector is written. */
        rw_command(0x0a, 99, 1);
        wr(TCR, SCSI_DATA_OUT);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE - 1; ++i)
                send_byte(0);
        wr(DATA, 0);
        wr(ICR, 0x11);
        wr(ICR, 0x80);
        wr(ICR, 0);
        (void)rd(CLEAR_IRQ);

        rw_command(0x08, 100, 1);
        wr(TCR, SCSI_DATA_IN);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                CHECK(receive_byte() == pattern(100, i, false));
        finish(0);

        rw_command(0x0a, 98, 1);
        wr(TCR, SCSI_DATA_OUT);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                send_byte(pattern(98, i, true));
        finish(0);

        rw_command(0x0a, 100, 2);
        expect_phase(SCSI_DATA_OUT);
        wr(TCR, SCSI_DATA_OUT);
        wr(ICR, 1);
        cpu_write_byte(DACK + 1, 0xff);  /* DMA mode/start not set. */
        wr(MODE, 2);
        wr(BSR, 0);
        unsigned int a = pattern(100, 0, true), b = pattern(100, 1, true);
        CHECK(rd(BSR) & DRQ);
        cpu_write_long(DACK, (a << 16) | b);
        for (unsigned int i = 2; i < 2 * SCSI_SECTOR_SIZE; ++i) {
                CHECK(rd(BSR) & DRQ);
                cpu_write_byte(DACK + 1, pattern(100 + i / SCSI_SECTOR_SIZE, i % SCSI_SECTOR_SIZE, true));
        }
        expect_phase(SCSI_STATUS);
        CHECK((rd(BSR) & (DRQ | IRQ | MATCH)) == (DRQ | IRQ));  /* Last send drained. */
        cpu_write_byte(DACK + 1, 0xff);  /* STATUS must not be consumed by a DMA send. */
        wr(MODE, 0);
        finish(0);

        const uint8_t write10[] = {0x2a, 0, 0, 0, 0, 102, 0, 0, 1, 0};
        command(write10, sizeof(write10));
        wr(TCR, SCSI_DATA_OUT);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                send_byte(pattern(102, i, true));
        finish(0);

        ncr5380_attach(NULL);
        CHECK(scsi_image_close(&image) == 0);
        CHECK(scsi_image_open(&image, image_path, false) == 0);
        attach_disk(&disk, &image.media);
        for (unsigned int lba = 97; lba < 104; ++lba) {
                uint8_t sector[SCSI_SECTOR_SIZE];
                CHECK(pread(image.fd, sector, sizeof(sector), lba * sizeof(sector)) == (ssize_t)sizeof(sector));
                for (unsigned int i = 0; i < sizeof(sector); ++i)
                        CHECK(sector[i] == pattern(lba, i, lba == 98 || (lba >= 100 && lba <= 102)));
        }

        /* Count 0 reads exactly 256 blocks, through the last block of the
         * reopened image. Neighboring blocks (including aborted 99) survive. */
        rw_command(0x08, IMAGE_BLOCKS - 256, 0);
        wr(TCR, SCSI_DATA_IN);
        wr(MODE, 2);
        wr(CLEAR_IRQ, 0);
        for (unsigned int lba = IMAGE_BLOCKS - 256; lba < IMAGE_BLOCKS; ++lba)
                for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                        CHECK(cpu_read_byte(DACK) == pattern(lba, i, lba == 98 || (lba >= 100 && lba <= 102)));
        wr(MODE, 0);
        finish(0);

        uint8_t read10[] = {0x28, 0, 0, 0, (IMAGE_BLOCKS - 2) >> 8,
                            (IMAGE_BLOCKS - 2) & 0xff, 0, 0, 2, 0};
        command(read10, sizeof(read10));
        wr(TCR, SCSI_DATA_IN);
        for (unsigned int lba = IMAGE_BLOCKS - 2; lba < IMAGE_BLOCKS; ++lba)
                for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                        CHECK(receive_byte() == pattern(lba, i, false));
        finish(0);

        read10[2] = 1;                   /* The LBA's high byte must not be truncated. */
        command(read10, sizeof(read10));
        finish(2);
        read_sense(5, 0x21);
        read10[8] = 0;
        command(read10, sizeof(read10));
        finish(0);                       /* A ten-byte CDB with count zero transfers nothing. */

        rw_command(0x08, IMAGE_BLOCKS - 1, 2);
        finish(2);
        read_sense(5, 0x21);
        read_sense(0, 0);
        rw_command(0x0a, 100, 1);
        finish(2);
        read_sense(7, 0x27);
        command(write10, sizeof(write10));
        finish(2);
        read_sense(7, 0x27);

        const uint8_t mode_sense[] = {0x1a, 0x08, 0x3f, 0, 4, 0};
        command(mode_sense, sizeof(mode_sense));
        wr(TCR, SCSI_DATA_IN);
        CHECK(receive_byte() == 3 && receive_byte() == 0 &&
              receive_byte() == 0x80 && receive_byte() == 0);
        finish(0);
        const uint8_t inquiry[] = {0x12, 0, 0, 0, 5, 0};
        command(inquiry, sizeof(inquiry));
        wr(TCR, SCSI_DATA_IN);
        CHECK(receive_byte() == 0 && receive_byte() == 0 && receive_byte() == 2 &&
              receive_byte() == 2 && receive_byte() == 31);
        finish(0);
        const uint8_t unsupported[] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        command(unsupported, sizeof(unsupported));
        finish(2);
        read_sense(5, 0x20);

        CHECK(truncate(image_path, SCSI_SECTOR_SIZE + 257) == 0);
        rw_command(0x08, 1, 1);
        finish(2);
        read_sense(3, 0x11);
        ncr5380_attach(NULL);
        CHECK(scsi_image_close(&image) == 0);
        attach_disk(&disk, NULL);
        command(tur, sizeof(tur));
        finish(2);
        read_sense(2, 0x3a);
        ncr5380_attach(NULL);
        CHECK(unlink(image_path) == 0);
        CHECK(scsi_image_open(&image, image_path, false) == -1 && errno == ENOENT);
}

struct failing_media {
        bool write_fails;
        unsigned int writes, flushes;
};

static int unused_read(void *ctx, uint32_t lba, uint8_t sector[SCSI_SECTOR_SIZE])
{
        (void)ctx;
        (void)lba;
        (void)sector;
        return -1;
}

static int fake_write(void *ctx, uint32_t lba, const uint8_t sector[SCSI_SECTOR_SIZE])
{
        struct failing_media *media = ctx;
        ++media->writes;
        CHECK(lba == 0 && sector[0] == 0x57 && sector[SCSI_SECTOR_SIZE - 1] == 0x57);
        return media->write_fails ? -1 : 0;
}

static int fail_flush(void *ctx)
{
        ++((struct failing_media *)ctx)->flushes;
        return -1;
}

static void test_disk_backend(void)
{
        struct failing_media mock = { .write_fails = true };
        struct scsi_disk_media media = { .ctx = &mock, .blocks = 1, .read = unused_read,
                .write = fake_write, .flush = fail_flush };
        struct scsi_disk disk;
        scsi_disk_init(&disk, &media);
        struct scsi_target target = scsi_disk_target(&disk, 1);

        const uint8_t start[][6] = {{0x0a, 0, 0, 0, 1, 0}, {0x03, 0, 0, 0, 18, 0}};
        for (unsigned int run = 0; run < 2; ++run) {
                mock.write_fails = run == 0;
                target.reset(target.ctx, false);
                enum scsi_phase next = SCSI_COMMAND;
                for (unsigned int i = 0; i < 6; ++i)
                        next = target.transfer(target.ctx, next, start[0][i]);
                CHECK(next == SCSI_DATA_OUT);
                for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i) {
                        CHECK(next == SCSI_DATA_OUT);
                        next = target.transfer(target.ctx, next, 0x57);
                }
                CHECK(next == SCSI_STATUS && target.peek(target.ctx, next) == 2);
                CHECK(mock.writes == run + 1 && mock.flushes == run);

                target.reset(target.ctx, false);
                next = SCSI_COMMAND;
                for (unsigned int i = 0; i < 6; ++i)
                        next = target.transfer(target.ctx, next, start[1][i]);
                for (unsigned int i = 0; i < 18; ++i) {
                        CHECK(next == SCSI_DATA_IN);
                        uint8_t data = target.peek(target.ctx, next);
                        if (i == 2) CHECK(data == 3);
                        if (i == 12) CHECK(data == 0x0c);
                        next = target.transfer(target.ctx, next, data);
                }
                CHECK(next == SCSI_STATUS && target.peek(target.ctx, next) == 0);
        }
}

static void test_plus_rom_decode(void)
{
        uint8_t *ram = calloc(1, RAM_SIZE);
        uint8_t *rom = calloc(1, ROM_SIZE);
        CHECK(ram && rom);
        memcpy(rom, "\x12\x34\x56\x78", 4);
        rom[ROM_SIZE - 2] = 0x9a;
        rom[ROM_SIZE - 1] = 0xbc;
        CHECK(umac_init(ram, rom, NULL) == 0);

        /* These are the two addresses the Plus ROM uses to detect SCSI. */
        CHECK(cpu_read_long(0x420000) != cpu_read_long(0x440000));
        CHECK(cpu_read_long(0x440000) == 0x12345678);
        CHECK(cpu_read_byte(0x420000) == 0xff && cpu_read_word(0x420000) == 0xffff);
        CHECK(cpu_read_long(0x41fffe) == 0x9abcffff);
        CHECK(cpu_read_instr_word(0x440000) == 0x1234);
        CHECK(cpu_read_instr_word(0x420000) == 0xffff);
        CHECK(cpu_read_long(0) == 0x12345678 && cpu_read_long(0x020000) == 0xffffffffu);
        free(rom);
        free(ram);
}

/* Calls the low-level SCSI Manager routines in an unmodified, user-supplied
 * Plus V3 ROM. This is not a boot test. Offsets are from the Plus ROM listing
 * linked in the uMac README. */
enum { ROM_RETURN = RAM_HIGH_ADDR + 0x100, ROM_STACK = RAM_HIGH_ADDR + 0x800,
       ROM_CDB = RAM_HIGH_ADDR + 0x1000, ROM_BUFFER = RAM_HIGH_ADDR + 0x2000 };

static void rom_call(unsigned int offset)
{
        cpu_write_long(ROM_STACK, ROM_RETURN);
        m68k_set_reg(M68K_REG_SP, ROM_STACK);
        m68k_set_reg(M68K_REG_PC, ROM_ADDR + offset);
        m68k_execute(100000);
        CHECK(m68k_get_reg(NULL, M68K_REG_PC) == ROM_RETURN);
        CHECK((m68k_get_reg(NULL, M68K_REG_D0) & 0xffff) == 0);
}

static void rom_rw_sector(bool write)
{
        rom_call(0x172bc);              /* Arbitrate. */
        m68k_set_reg(M68K_REG_D0, 1);
        rom_call(0x17306);              /* Select ID 1. */
        const uint8_t cdb[] = {write ? 0x0a : 0x08, 0, 0, 100, 1, 0};
        for (unsigned int i = 0; i < sizeof(cdb); ++i)
                cpu_write_byte(ROM_CDB + i, cdb[i]);
        m68k_set_reg(M68K_REG_A2, ROM_CDB);
        m68k_set_reg(M68K_REG_D2, sizeof(cdb));
        rom_call(0x1734c);              /* Send CDB with REQ/ACK. */
        m68k_set_reg(M68K_REG_A2, ROM_BUFFER);
        m68k_set_reg(M68K_REG_D2, SCSI_SECTOR_SIZE);
        m68k_set_reg(M68K_REG_D4, write ? 0x8080 : 0); /* Blind send / polled receive. */
        rom_call(0x1739c);              /* Transfer data through DACK. */
        rom_call(0x17464);              /* STATUS */
        CHECK((m68k_get_reg(NULL, M68K_REG_D2) & 0xff) == 0);
        rom_call(0x1749a);              /* MESSAGE IN */
        CHECK((m68k_get_reg(NULL, M68K_REG_D2) & 0xff) == 0);
        expect_phase(SCSI_BUS_FREE);
}

static void test_original_rom(const char *rom_path)
{
        uint8_t *rom = malloc(ROM_SIZE);
        uint8_t *ram = calloc(1, RAM_SIZE);
        FILE *file = fopen(rom_path, "rb");
        CHECK(file && rom && ram);
        CHECK(fread(rom, 1, ROM_SIZE, file) == ROM_SIZE && fgetc(file) == EOF && !ferror(file));
        CHECK(fclose(file) == 0 && !memcmp(rom, "\x4d\x1f\x81\x72", 4));
        umac_init(ram, rom, NULL);
        cpu_write_word(ROM_RETURN, 0x60fe);       /* BRA.S to self. */
        m68k_set_reg(M68K_REG_A3, SCSI);
        m68k_set_reg(M68K_REG_A4, SCSI + 1);
        m68k_set_reg(M68K_REG_D7, 0);

        int fd = make_image();
        CHECK(ftruncate(fd, 101 * SCSI_SECTOR_SIZE) == 0 && close(fd) == 0);
        struct scsi_image image;
        CHECK(scsi_image_open(&image, image_path, true) == 0);
        struct scsi_disk disk;
        attach_disk(&disk, &image.media);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                cpu_write_byte(ROM_BUFFER + i, pattern(100, i, true));
        rom_rw_sector(true);
        ncr5380_attach(NULL);
        CHECK(scsi_image_close(&image) == 0);
        CHECK(scsi_image_open(&image, image_path, false) == 0);
        attach_disk(&disk, &image.media);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                cpu_write_byte(ROM_BUFFER + i, 0);
        rom_rw_sector(false);
        for (unsigned int i = 0; i < SCSI_SECTOR_SIZE; ++i)
                CHECK(cpu_read_byte(ROM_BUFFER + i) == pattern(100, i, true));
        ncr5380_attach(NULL);
        CHECK(scsi_image_close(&image) == 0);
        free(rom);
        free(ram);
}

int main(int argc, char **argv)
{
        if (argc == 2 && !strcmp(argv[1], "controller")) {
                test_controller();
                test_plus_rom_decode();
        } else if (argc == 2 && !strcmp(argv[1], "disk")) {
                test_disk_backend();
                test_disk();
        } else if (argc == 3 && !strcmp(argv[1], "rom")) {
                test_original_rom(argv[2]);
        } else {
                fprintf(stderr, "Usage: %s controller | disk | rom <Mac Plus V3 ROM>\n", argv[0]);
                return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
}
