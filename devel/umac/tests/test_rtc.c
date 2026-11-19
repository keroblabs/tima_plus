/* Isolated unit tests for the Macintosh Plus serial RTC + PRAM core
 * (PLUS-EMU-0009).  Drives the rtc.c serial state machine directly at
 * the pin level with an injected fixed host clock; no VIA or SDL.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rtc.h"

#define CHECK(expr) do {                                                       \
        if (!(expr)) {                                                         \
                fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #expr);\
                exit(EXIT_FAILURE);                                            \
        }                                                                      \
} while (0)

/* Injected host clock. */
static int64_t g_host_seconds = 0;
static int64_t host_cb(void *ctx) { (void)ctx; return g_host_seconds; }

/* Serial driver mirroring the Mac side of the VIA Port B transfer. */
struct drv {
        rtc_t *r;
        int enb;
};

static void drv_pins(struct drv *d, int clk, int data, int out)
{
        rtc_via_update(d->r, d->enb, clk, data, out);
}

static void drv_sel(struct drv *d)
{
        d->enb = 0;
        drv_pins(d, 0, 0, 1);
}

static void drv_desel(struct drv *d)
{
        d->enb = 1;
        drv_pins(d, 0, 0, 1);
}

/* Clock out eight bits (MSB first) that the Mac drives to the RTC. */
static void drv_send(struct drv *d, uint8_t byte)
{
        for (int i = 7; i >= 0; i--) {
                int bit = (byte >> i) & 1;
                drv_pins(d, 1, bit, 1);   /* clock high, data set */
                drv_pins(d, 0, bit, 1);   /* falling edge: RTC samples */
        }
}

/* Clock in eight bits the RTC drives back to the Mac (MSB first). */
static uint8_t drv_recv(struct drv *d)
{
        uint8_t v = 0;
        for (int i = 7; i >= 0; i--) {
                drv_pins(d, 1, 0, 0);     /* PB0 input (RTC drives) */
                drv_pins(d, 0, 0, 0);     /* falling edge: RTC presents bit */
                v = (uint8_t)((v << 1) | rtc_data_out(d->r));
        }
        return v;
}

static uint8_t reg_read(struct drv *d, uint8_t cmd)
{
        drv_sel(d);
        drv_send(d, cmd);
        uint8_t v = drv_recv(d);
        drv_desel(d);
        return v;
}

static void reg_write(struct drv *d, uint8_t cmd, uint8_t value)
{
        drv_sel(d);
        drv_send(d, cmd);
        drv_send(d, value);
        drv_desel(d);
}

static uint8_t ext_read(struct drv *d, uint8_t addr)
{
        uint8_t prefix = 0xB8 | ((addr >> 5) & 0x07);
        uint8_t arg = (uint8_t)(((addr & 0x1F) << 2) | 0x01);
        drv_sel(d);
        drv_send(d, prefix);
        drv_send(d, arg);
        uint8_t v = drv_recv(d);
        drv_desel(d);
        return v;
}

static void ext_write(struct drv *d, uint8_t addr, uint8_t value)
{
        uint8_t prefix = 0x38 | ((addr >> 5) & 0x07);
        uint8_t arg = (uint8_t)(((addr & 0x1F) << 2) | 0x01);
        drv_sel(d);
        drv_send(d, prefix);
        drv_send(d, arg);
        drv_send(d, value);
        drv_desel(d);
}

static uint32_t read_seconds(struct drv *d)
{
        uint32_t b0 = reg_read(d, 0x81);
        uint32_t b1 = reg_read(d, 0x85);
        uint32_t b2 = reg_read(d, 0x89);
        uint32_t b3 = reg_read(d, 0x8D);
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

/**********************************************************************/

static void test_clock_read(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 0;
        rtc_init(&r, &tp);

        /* Host Unix 0 + default offset => 1904 epoch = 0x7C25B080. */
        uint32_t s = read_seconds(&d);
        CHECK(s == 0x7C25B080u);
        CHECK(reg_read(&d, 0x81) == 0x80);
        CHECK(reg_read(&d, 0x85) == 0xB0);
        CHECK(reg_read(&d, 0x89) == 0x25);
        CHECK(reg_read(&d, 0x8D) == 0x7C);

        /* Alias registers 0x91/0x95/0x99/0x9D mirror seconds bytes. */
        CHECK(reg_read(&d, 0x91) == 0x80);
        CHECK(reg_read(&d, 0x95) == 0xB0);
        CHECK(reg_read(&d, 0x99) == 0x25);
        CHECK(reg_read(&d, 0x9D) == 0x7C);

        /* Progression: counter advances with host time, no read needed. */
        g_host_seconds = 100;
        CHECK(read_seconds(&d) == 0x7C25B080u + 100u);

        printf("  test_clock_read OK\n");
}

static void test_clock_write(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 1000;
        rtc_init(&r, &tp);

        /* Guest sets all four seconds bytes to a known value. */
        uint32_t want = 0x11223344u;
        reg_write(&d, 0x01, 0x44);
        reg_write(&d, 0x05, 0x33);
        reg_write(&d, 0x09, 0x22);
        reg_write(&d, 0x0D, 0x11);
        CHECK(read_seconds(&d) == want);

        /* Host wall clock is never modified by a guest write. */
        CHECK(g_host_seconds == 1000);

        /* Guest-set value keeps ticking from the host clock. */
        g_host_seconds = 1005;
        CHECK(read_seconds(&d) == want + 5u);

        printf("  test_clock_write OK\n");
}

static void test_wrap(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 0;
        rtc_init(&r, &tp);

        /* Set near the 32-bit boundary and step across it. */
        reg_write(&d, 0x01, 0xFE);
        reg_write(&d, 0x05, 0xFF);
        reg_write(&d, 0x09, 0xFF);
        reg_write(&d, 0x0D, 0xFF);
        CHECK(read_seconds(&d) == 0xFFFFFFFEu);
        g_host_seconds = 3;              /* +3 wraps past 2^32 */
        CHECK(read_seconds(&d) == 0x00000001u);

        printf("  test_wrap OK\n");
}

static void test_pram_aliasing(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 0;
        rtc_init(&r, &tp);

        /* Legacy write of PRAM[0x00] via 0x41, read via 0xC1 and ext. */
        reg_write(&d, 0x41, 0xA5);
        CHECK(reg_read(&d, 0xC1) == 0xA5);
        CHECK(ext_read(&d, 0x00) == 0xA5);

        /* PRAM[0x10]: legacy 0x21 write <-> extended read at 0x10. */
        reg_write(&d, 0x21, 0x5A);
        CHECK(reg_read(&d, 0xA1) == 0x5A);
        CHECK(ext_read(&d, 0x10) == 0x5A);

        /* Extended write, legacy read back of same byte. */
        ext_write(&d, 0x00, 0x3C);
        CHECK(reg_read(&d, 0xC1) == 0x3C);

        printf("  test_pram_aliasing OK\n");
}

static void test_pram_extended(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 0;
        rtc_init(&r, &tp);

        const uint8_t addrs[] = { 0x00, 0x13, 0x1F, 0x20, 0xFF };
        for (size_t i = 0; i < sizeof(addrs); i++)
                ext_write(&d, addrs[i], (uint8_t)(0x10 + i));
        for (size_t i = 0; i < sizeof(addrs); i++)
                CHECK(ext_read(&d, addrs[i]) == (uint8_t)(0x10 + i));

        printf("  test_pram_extended OK\n");
}

static void test_write_protect(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 0;
        rtc_init(&r, &tp);

        ext_write(&d, 0x00, 0x11);
        CHECK(ext_read(&d, 0x00) == 0x11);

        /* Protect: 0x35 0x80.  Writes rejected, reads still work. */
        reg_write(&d, 0x35, 0x80);
        ext_write(&d, 0x00, 0x22);
        reg_write(&d, 0x41, 0x33);
        reg_write(&d, 0x01, 0xAB);               /* seconds write blocked */
        CHECK(ext_read(&d, 0x00) == 0x11);
        CHECK((read_seconds(&d) & 0xff) == 0x80);

        /* Unprotect: 0x35 0x00.  Writes succeed again. */
        reg_write(&d, 0x35, 0x00);
        ext_write(&d, 0x00, 0x44);
        CHECK(ext_read(&d, 0x00) == 0x44);

        printf("  test_write_protect OK\n");
}

static void test_abort(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };

        g_host_seconds = 0;
        rtc_init(&r, &tp);
        ext_write(&d, 0x00, 0x77);

        /* Begin a write of PRAM[0x00] but deselect partway through. */
        drv_sel(&d);
        drv_send(&d, 0x41);              /* write PRAM[0x00] command */
        /* send only 4 data bits */
        for (int i = 7; i >= 4; i--) {
                drv_pins(&d, 1, 1, 1);
                drv_pins(&d, 0, 1, 1);
        }
        drv_desel(&d);                   /* abort */

        /* The partial transfer must not have completed a write. */
        CHECK(ext_read(&d, 0x00) == 0x77);

        printf("  test_abort OK\n");
}

static void test_persistence(void)
{
        rtc_t r;
        struct rtc_time_provider tp = { host_cb, NULL };
        struct drv d = { &r, 1 };
        char path[] = "/tmp/umac_rtc_test_XXXXXX";
        int fd = mkstemp(path);
        CHECK(fd >= 0);
        close(fd);

        g_host_seconds = 2000;
        rtc_init(&r, &tp);
        ext_write(&d, 0x00, 0xDE);
        ext_write(&d, 0xFF, 0xAD);
        reg_write(&d, 0x01, 0x99);       /* guest-set clock offset */
        CHECK(rtc_pram_save(&r, path) == 0);

        /* Reload into a fresh instance: PRAM + clock offset restored. */
        rtc_t r2;
        struct drv d2 = { &r2, 1 };
        rtc_init(&r2, &tp);
        CHECK(rtc_pram_load(&r2, path) == 0);
        CHECK(ext_read(&d2, 0x00) == 0xDE);
        CHECK(ext_read(&d2, 0xFF) == 0xAD);
        CHECK((read_seconds(&d2) & 0xff) == 0x99);

        /* Warm reset preserves PRAM and clock. */
        rtc_reset(&r2);
        CHECK(ext_read(&d2, 0x00) == 0xDE);

        /* Missing file: load fails, instance keeps cold defaults. */
        rtc_t r3;
        rtc_init(&r3, &tp);
        CHECK(rtc_pram_load(&r3, "/tmp/umac_rtc_does_not_exist_zzz") != 0);

        /* Corrupt/truncated file: load rejects it. */
        FILE *cf = fopen(path, "wb");
        CHECK(cf != NULL);
        fputs("garbage", cf);
        fclose(cf);
        CHECK(rtc_pram_load(&r3, path) != 0);

        remove(path);
        printf("  test_persistence OK\n");
}

int main(void)
{
        printf("RTC core tests:\n");
        test_clock_read();
        test_clock_write();
        test_wrap();
        test_pram_aliasing();
        test_pram_extended();
        test_write_protect();
        test_abort();
        test_persistence();
        printf("All RTC core tests passed.\n");
        return 0;
}
