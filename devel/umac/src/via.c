/* umac VIA emulation
 *
 * Bare minimum support for ports A/B, shift register, and IRQs.
 * A couple of Mac-specific assumptions in here, as per comments...
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>

#include "machw.h"
#include "via.h"

#ifdef __DEBUG
#define VDBG(...)       printf(__VA_ARGS__)
#else
#define VDBG(...)       do {} while(0)
#endif

#define VERR(...)       fprintf(stderr, __VA_ARGS__)

#define VIA_RB          0
#define VIA_RA          1
#define VIA_DDRB        2
#define VIA_DDRA        3
#define VIA_T1CL        4
#define VIA_T1CH        5
#define VIA_T1LL        6
#define VIA_T1LH        7
#define VIA_T2CL        8
#define VIA_T2CH        9
#define VIA_SR          10
#define VIA_ACR         11
#define VIA_PCR         12
#define VIA_IFR         13
#define  VIA_IRQ_CA     0x01
#define  VIA_IRQ_CB     0x02
#define  VIA_IRQ_SR     0x04
#define  VIA_IRQ_T2     0x20
#define  VIA_IRQ_T1     0x40
#define VIA_IER         14
#define VIA_RA_ALT      15 // No-handshake version

static const char *dbg_regnames[] = {
        "VIA_RB",
        "VIA_RA",
        "VIA_DDRB",
        "VIA_DDRA",
        "VIA_T1CL",
        "VIA_T1CH",
        "VIA_T1LL",
        "VIA_T1LH",
        "VIA_T2CL",
        "VIA_T2CH",
        "VIA_SR",
        "VIA_ACR",
        "VIA_PCR",
        "VIA_IFR",
        "VIA_IER",
        "VIA_RA_ALT",
};

static uint8_t via_regs[16];
static struct via_cb via_callbacks;
static int irq_status = 0;
static uint8_t irq_active = 0;
static uint8_t irq_enable = 0;
static int sr_tx_pending = -1;
static uint64_t via_clock;

struct via_timer {
        uint16_t latch;
        uint16_t counter;
        bool delay, reload;
        bool running, armed;
};

static struct via_timer timer1, timer2;

void    via_init(struct via_cb *cb)
{
        for (int i = 0; i < 16; i++)
                via_regs[i] = 0;
        via_regs[VIA_RA] = 0x10; // Overlay, FIXME
        via_callbacks = cb ? *cb : (struct via_cb){0};
        irq_status = irq_active = irq_enable = 0;
        sr_tx_pending = -1;
        via_clock = 0;
        timer1 = timer2 = (struct via_timer){0};
}

static void via_update_rega(uint8_t data)
{
        if ((via_regs[VIA_RA] ^ data) && via_callbacks.ra_change)
                via_callbacks.ra_change(data);
}

static void via_update_regb(uint8_t data)
{
        if ((via_regs[VIA_RB] ^ data) && via_callbacks.rb_change)
                via_callbacks.rb_change(data);
}

static void via_update_sr(uint8_t data)
{
        /* Mac assumption: SR active when ACR SR control selects
         * external clock:
         */
        if ((via_regs[VIA_ACR] & 0x1c) == 0x1c) {
                if (sr_tx_pending >= 0) {
                        /* Doh! */
                        VDBG("[VIA: SR send whilst send (%02x) active!]\n", sr_tx_pending);
                }
                /* When SR's written, the ROM'll wait for the IRQ
                 * indicating the byte's transmitted.  At that point,
                 * it'll expect a response (but not too soon).  So,
                 * flag that the TX occurred, and mark tbe byte
                 * pending to deal with in the runloop "a little bit
                 * later" -- the response seems to get lost if it's
                 * reflected back too soon.
                 */
                sr_tx_pending = data;
                irq_active |= VIA_IRQ_SR;
        } else if ((via_regs[VIA_ACR] & 0x1c) == 0x18) {
                /* Mac sends a byte of zeroes fuelled by phi2, as a
                 * method to pull KbdData low (to get the kbd's
                 * attention).  The d/s implies SRMC=110 completion
                 * should also trigger IRQ vector 2, but empirically
                 * this screws things up and code doesn't seem to
                 * expect it.  So don't trigger IRQ.
                 */
                VDBG("[VIA: SR send (val %02x)]\n", data);
                via_regs[VIA_SR] = 0;
        }
}

/* Called when the VIA_IRQ_SR interrupt is acknowledged, i.e. that the
 * Mac's aware of the last TX/RX.  We can then use it to pace out the
 * transmit callback action -- the response then cannot race with the
 * IRQ showing the TX has completed.
 */
static void via_sr_done(void)
{
        if (sr_tx_pending >= 0) {
                uint8_t data = (uint8_t)sr_tx_pending;
                sr_tx_pending = -1;
                if (via_callbacks.sr_tx)
                        via_callbacks.sr_tx(data);
        }
}

// 6 Timer 1
// 5 Timer 2
// 4 Keyboard clock
// 3 Keyboard data bit
// 2 Keyboard data ready
// 1 CA2: Vertical blanking interrupt
// 0 CA1: One-second interrupt

static void via_assess_irq(void)
{
        int irq = 0;
        static uint8_t last_active = 0;
        uint8_t active = irq_enable & irq_active & 0x7f;
        irq = active != 0;

        if (active != last_active) {
                VDBG("[VIA: IRQ state now %02x]\n", active);
                last_active = active;
        }
        if (irq != irq_status) {
                if (via_callbacks.irq_set)
                        via_callbacks.irq_set(irq);
                irq_status = irq;
        }
}

static void timer_load(struct via_timer *timer, uint8_t high)
{
        timer->latch = (high << 8) | (timer->latch & 0xff);
        timer->counter = timer->latch;
        timer->running = timer->armed = timer->delay = true;
        timer->reload = false;
}

static void timer_advance(struct via_timer *timer, uint64_t clocks, bool continuous, uint8_t flag)
{
        if (!timer->running || !clocks)
                return;

        if (timer->delay) {
                if (timer->reload)
                        timer->counter = timer->latch;
                timer->reload = timer->delay = false;
                --clocks;
        }

        if (clocks <= timer->counter) {
                timer->counter -= clocks;
                return;
        }

        clocks -= timer->counter + 1u;
        if (timer->armed)
                irq_active |= flag;
        if (continuous && timer->armed) {
                unsigned int phase = clocks % (timer->latch + 2u);
                timer->counter = phase ? timer->latch - (phase - 1) : UINT16_MAX;
                timer->reload = timer->delay = phase == 0;
        } else {
                timer->counter = (uint16_t)(UINT16_MAX - clocks);
                timer->armed = false;
        }
}

/* A[12:9] select regs */
void    via_write(unsigned int address, uint8_t data)
{
        unsigned int r = (address >> 9) & 0xf;
        const char *rname = dbg_regnames[r];
        (void)rname; // Unused if !DEBUG
        VDBG("[VIA: WR %02x -> %s (0x%x)]\n", data, rname, r);

        int dowrite = 1;
        switch (r) {
        case VIA_RA:
        case VIA_RA_ALT:
                via_update_rega(data);
                r = VIA_RA;
                break;
        case VIA_RB:
                via_update_regb(data);
                break;
        case VIA_DDRA:
                break; // FIXME
        case VIA_DDRB:
                break; // Stored below; rb_pins notified after store.
        case VIA_T1CL:
        case VIA_T1LL:
                timer1.latch = (timer1.latch & 0xff00) | data;
                break;
        case VIA_T1CH:
                timer_load(&timer1, data);
                irq_active &= ~VIA_IRQ_T1;
                break;
        case VIA_T1LH:
                timer1.latch = (data << 8) | (timer1.latch & 0xff);
                irq_active &= ~VIA_IRQ_T1;
                break;
        case VIA_T2CL:
                timer2.latch = (timer2.latch & 0xff00) | data;
                break;
        case VIA_T2CH:
                timer_load(&timer2, data);
                irq_active &= ~VIA_IRQ_T2;
                break;
        case VIA_SR:
                via_update_sr(data);
                dowrite = 0;
                break;
        case VIA_IER:
                if (data & 0x80)
                        irq_enable |= data & 0x7f;
                else
                        irq_enable &= ~(data & 0x7f);
                break;
        case VIA_IFR: {
                int which_acked = (irq_active & data);
                irq_active &= ~data;
                /* If ISR is acking the SR IRQ, a TX or RX is complete,
                 * and we might want to then trigger other actions.
                 */
                if (which_acked & VIA_IRQ_SR)
                        via_sr_done();
        } break;
        case VIA_PCR:
                VDBG("VIA PCR %02x\n", data);
                break;
        default:
                VDBG("[VIA: unhandled WR %02x to %s (reg 0x%x)]\n", data, rname, r);
        }

        if (dowrite)
                via_regs[r] = data;

        /* Notify a direction-sensitive Port B device of resolved pin
         * state on ORB or DDRB writes (fires even on DDRB-only
         * changes, which rb_change does not). */
        if ((r == VIA_RB || r == VIA_DDRB) && via_callbacks.rb_pins)
                via_callbacks.rb_pins(via_regs[VIA_RB], via_regs[VIA_DDRB]);
        if ((r == VIA_RA || r == VIA_DDRA) && via_callbacks.ra_pins)
                via_callbacks.ra_pins(via_regs[VIA_RA], via_regs[VIA_DDRA]);

        via_assess_irq();
}

uint8_t via_read_ifr(void)
{
        uint8_t active = irq_enable & irq_active & 0x7f;
        return irq_active | (active ? 0x80 : 0);
}

static uint8_t via_read_rega(void)
{
        uint8_t data = (via_callbacks.ra_in) ? via_callbacks.ra_in() : 0;
        uint8_t ddr = via_regs[VIA_DDRA];
        // DDR=1 is output, so take ORA version:
        return (ddr & via_regs[VIA_RA]) | (~ddr & data);
}

static uint8_t via_read_regb(void)
{
        uint8_t data = (via_callbacks.rb_in) ? via_callbacks.rb_in() : 0;
        uint8_t ddr = via_regs[VIA_DDRB];
        return (ddr & via_regs[VIA_RB]) | (~ddr & data);
}

uint8_t via_read(unsigned int address)
{
        unsigned int r = (address >> 9) & 0xf;
        const char *rname = dbg_regnames[r];
        uint8_t data = via_regs[r];
        (void)rname; // Unused if !DEBUG

        switch (r) {
        case VIA_RA:
        case VIA_RA_ALT:
                data = via_read_rega();
                break;

        case VIA_RB:
                data = via_read_regb();
                break;

        case VIA_T1CL:
                irq_active &= ~VIA_IRQ_T1;
                data = timer1.counter;
                break;
        case VIA_T1CH:
                data = timer1.counter >> 8;
                break;
        case VIA_T1LL:
                data = timer1.latch;
                break;
        case VIA_T1LH:
                data = timer1.latch >> 8;
                break;
        case VIA_T2CL:
                irq_active &= ~VIA_IRQ_T2;
                data = timer2.counter;
                break;
        case VIA_T2CH:
                data = timer2.counter >> 8;
                break;
        case VIA_SR:
                irq_active &= ~0x04;
                break;

        case VIA_IER:
                data = 0x80 | irq_enable;
                break;

        case VIA_IFR:
                data = via_read_ifr();
                break;
        default:
                VDBG("[VIA: unhandled RD of %s (reg 0x%x)]\n", rname, r);
        }
        VDBG("[VIA: RD %02x <- %s (0x%x)]\n", data, rname, r);
        via_assess_irq();
        return data;
}

void    via_advance(uint64_t c8m_ticks)
{
        uint64_t clock = c8m_ticks / MAC_VIA_DIVISOR;
        if (clock <= via_clock)
                return;

        uint64_t clocks = clock - via_clock;
        via_clock = clock;
        timer_advance(&timer1, clocks, via_regs[VIA_ACR] & 0x40, VIA_IRQ_T1);
        /* The Plus ROM uses timed T2 mode for TACH. Pulse-counting on PB6
         * requires a horizontal blanking pin source. */
        if (!(via_regs[VIA_ACR] & 0x20))
                timer_advance(&timer2, clocks, false, VIA_IRQ_T2);
        via_assess_irq();
}

static uint64_t timer_next_irq(const struct via_timer *timer, uint8_t flag)
{
        if (!timer->armed || !(irq_enable & flag) || (irq_active & flag))
                return UINT64_MAX;
        uint16_t count = timer->reload ? timer->latch : timer->counter;
        return (via_clock + count + 1 + timer->delay) * MAC_VIA_DIVISOR;
}

uint64_t via_next_timer_irq(void)
{
        uint64_t t1 = timer_next_irq(&timer1, VIA_IRQ_T1);
        uint64_t t2 = (via_regs[VIA_ACR] & 0x20) ? UINT64_MAX : timer_next_irq(&timer2, VIA_IRQ_T2);
        return t1 < t2 ? t1 : t2;
}

/* External world pipes CA1/CA2 events (passage of time) in here:
 */
void    via_caX_event(int ca)
{
        if (ca == 1) {
                irq_active |= VIA_IRQ_CA;
        } else if (ca == 2) {
                irq_active |= VIA_IRQ_CB;
        }
        via_assess_irq();
}

void    via_sr_rx(uint8_t val)
{
        /* If SR config in ACR is external (yes! a Mac assumption!)
         * then fill SR with value and trigger SR IRQ:
         */
        VDBG("[VIAL sr_rx %02x (acr %02x)]\n", val, via_regs[VIA_ACR]);
        if ((via_regs[VIA_ACR] & 0x1c) == 0x0c) {
                via_regs[VIA_SR] = val;
                irq_active |= VIA_IRQ_SR;
                VDBG("[VIA sr_rx received, IRQ pending]\n");
                via_assess_irq();
        } else {
                VDBG("[VIA ACR SR state %02x, not receiving]\n", via_regs[VIA_ACR]);
        }
}
