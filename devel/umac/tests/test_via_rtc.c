/* Integration test: VIA Port B serial plumbing + RTC (PLUS-EMU-0009).
 *
 * Exercises the real via.c register interface at the bus level,
 * wiring the RTC exactly as main.c does (rb_pins -> rtc_via_update,
 * rb_in -> rtc_data_out).  This validates that ORB/DDRB writes resolve
 * the PB2/PB1/PB0 pins (including DDRB turn-around) to the device and
 * that PB0 reads return the RTC bit only when PB0 is an input.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "via.h"
#include "rtc.h"

#define CHECK(expr) do {                                                       \
        if (!(expr)) {                                                         \
                fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #expr);\
                exit(EXIT_FAILURE);                                            \
        }                                                                      \
} while (0)

/* Canonical Macintosh Plus VIA register byte addresses. */
#define ADDR_ORB        0xEFE1FEu        /* r = 0  */
#define ADDR_DDRB       0xEFE5FEu        /* r = 2  */
#define ADDR_IFR        0xEFFBFEu        /* r = 13 */
#define ADDR_IER        0xEFFDFEu        /* r = 14 */

static rtc_t g_rtc;
static int64_t g_host_seconds = 0;
static int g_irq_state = 0;

static int64_t host_cb(void *ctx) { (void)ctx; return g_host_seconds; }

/* Bridge callbacks (mirror of main.c wiring). */
static void cb_rb_pins(uint8_t orb, uint8_t ddrb)
{
        rtc_via_update(&g_rtc, (orb >> 2) & 1, (orb >> 1) & 1, orb & 1,
                       ddrb & 1);
}
static uint8_t cb_rb_in(void)
{
        return (uint8_t)(rtc_data_out(&g_rtc) & 1);
}
static void cb_irq_set(int status) { g_irq_state = status; }

/* Mac-side serial helpers operating through the VIA. */
static void via_pins(int enb, int clk, int data)
{
        uint8_t orb = (uint8_t)((enb << 2) | (clk << 1) | (data & 1));
        via_write(ADDR_ORB, orb);
}

static void set_ddrb(uint8_t ddrb) { via_write(ADDR_DDRB, ddrb); }

static void bus_sel(void)
{
        set_ddrb(0x07);          /* PB2/PB1/PB0 outputs */
        via_pins(1, 0, 0);       /* deselected */
        via_pins(0, 0, 0);       /* assert enb low -> start */
}

static void bus_desel(void)
{
        via_pins(1, 0, 0);
}

static void bus_send(uint8_t byte)
{
        set_ddrb(0x07);
        for (int i = 7; i >= 0; i--) {
                int bit = (byte >> i) & 1;
                via_pins(0, 1, bit);
                via_pins(0, 0, bit);
        }
}

static uint8_t bus_recv(void)
{
        set_ddrb(0x06);          /* PB0 input (RTC drives), PB1/PB2 out */
        uint8_t v = 0;
        for (int i = 7; i >= 0; i--) {
                uint8_t bit = (uint8_t)(via_read(ADDR_ORB) & 1);
                v = (uint8_t)((v << 1) | bit);
                via_pins(0, 1, 0);
                via_pins(0, 0, 0);
        }
        set_ddrb(0x07);
        return v;
}

static uint8_t bus_reg_read(uint8_t cmd)
{
        bus_sel();
        bus_send(cmd);
        uint8_t v = bus_recv();
        bus_desel();
        return v;
}

static void bus_reg_write(uint8_t cmd, uint8_t value)
{
        bus_sel();
        bus_send(cmd);
        bus_send(value);
        bus_desel();
}

static uint8_t bus_ext_read(uint8_t addr)
{
        uint8_t prefix = (uint8_t)(0xB8 | ((addr >> 5) & 0x07));
        uint8_t arg = (uint8_t)(((addr & 0x1F) << 2) | 0x01);
        bus_sel();
        bus_send(prefix);
        bus_send(arg);
        uint8_t v = bus_recv();
        bus_desel();
        return v;
}

static void bus_ext_write(uint8_t addr, uint8_t value)
{
        uint8_t prefix = (uint8_t)(0x38 | ((addr >> 5) & 0x07));
        uint8_t arg = (uint8_t)(((addr & 0x1F) << 2) | 0x01);
        bus_sel();
        bus_send(prefix);
        bus_send(arg);
        bus_send(value);
        bus_desel();
}

static void test_clock_through_via(void)
{
        g_host_seconds = 0;
        uint32_t b0 = bus_reg_read(0x81);
        uint32_t b1 = bus_reg_read(0x85);
        uint32_t b2 = bus_reg_read(0x89);
        uint32_t b3 = bus_reg_read(0x8D);
        uint32_t s = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        CHECK(s == 0x7C25B080u);
        printf("  test_clock_through_via OK\n");
}

static void test_pram_through_via(void)
{
        /* Legacy write, legacy + extended read of the same byte. */
        bus_reg_write(0x41, 0x5A);
        CHECK(bus_reg_read(0xC1) == 0x5A);
        CHECK(bus_ext_read(0x00) == 0x5A);

        /* Extended write to a high address only reachable via extended. */
        bus_ext_write(0xFF, 0xC3);
        CHECK(bus_ext_read(0xFF) == 0xC3);
        printf("  test_pram_through_via OK\n");
}

static void test_ddrb_turnaround(void)
{
        /* Reference vector 8: after writing a known PB0 output latch,
         * flipping DDRB bit 0 to input must make PB0 reads reflect the
         * RTC-driven bit, not the stale output latch. */
        bus_reg_write(0x41, 0x80);       /* PRAM[0]=0x80 -> MSB read = 1 */

        bus_sel();
        bus_send(0xC1);                  /* read PRAM[0] */
        set_ddrb(0x07);
        via_pins(0, 0, 1);               /* drive PB0 latch = 1 */
        set_ddrb(0x06);                  /* turn PB0 to input */
        /* First response bit (MSB of 0x80) is 1; prove reads track the
         * device by reading the whole byte back. */
        uint8_t v = 0;
        for (int i = 7; i >= 0; i--) {
                v = (uint8_t)((v << 1) | (via_read(ADDR_ORB) & 1));
                via_pins(0, 1, 0);
                via_pins(0, 0, 0);
        }
        set_ddrb(0x07);
        bus_desel();
        CHECK(v == 0x80);

        /* With PB0 as output, a plain ORB read returns the latch, not
         * the device bit. */
        set_ddrb(0x07);
        via_pins(1, 0, 1);               /* deselected, PB0 latch = 1 */
        CHECK((via_read(ADDR_ORB) & 1) == 1);
        printf("  test_ddrb_turnaround OK\n");
}

static void test_one_second_irq(void)
{
        /* Enable one-second (IFR/IER bit 0); VBL is bit 1. */
        via_write(ADDR_IER, 0x81);       /* set-enable bit 0 */
        CHECK(g_irq_state == 0);

        via_caX_event(1);                /* one-second tick */
        CHECK(g_irq_state == 1);
        CHECK(via_read(ADDR_IFR) & 0x01);

        /* Ack it. */
        via_write(ADDR_IFR, 0x01);
        CHECK(g_irq_state == 0);
        CHECK(!(via_read(ADDR_IFR) & 0x01));

        /* VBL (bit 1) still works and is independent, even though its
         * IER bit is masked. */
        via_caX_event(2);
        CHECK(via_read(ADDR_IFR) & 0x02);
        via_write(ADDR_IFR, 0x02);
        printf("  test_one_second_irq OK\n");
}

int main(void)
{
        struct rtc_time_provider tp = { host_cb, NULL };
        struct via_cb cb = {
                .ra_change = NULL,
                .rb_change = NULL,
                .ra_in = NULL,
                .rb_in = cb_rb_in,
                .sr_tx = NULL,
                .irq_set = cb_irq_set,
                .rb_pins = cb_rb_pins,
        };

        rtc_init(&g_rtc, &tp);
        via_init(&cb);

        printf("VIA + RTC integration tests:\n");
        test_clock_through_via();
        test_pram_through_via();
        test_ddrb_turnaround();
        test_one_second_irq();
        printf("All VIA + RTC integration tests passed.\n");
        return 0;
}
