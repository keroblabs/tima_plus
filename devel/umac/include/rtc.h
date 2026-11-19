/* umac Macintosh Plus serial RTC + battery-backed PRAM
 *
 * Platform-neutral emulation of the Apple 343-0042-B family serial
 * real-time clock / parameter-RAM device used by the Macintosh Plus
 * (M0001AP).  The device is reached only through the 6522 VIA Port B
 * serial pins (PB2 rtcEnb, PB1 rtcClk, PB0 rtcData).
 *
 * See docs/tasks/db/343-0042-B-RTC-PRAM-reference.md and
 * docs/tasks/db/PLUS-EMU-0009.md.
 *
 * The core keeps host-file and wall-clock access behind an injectable
 * time provider so it can be unit-tested without SDL or the Unix
 * runtime.
 *
 * Copyright 2026 Tima/Plus contributors
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

#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* Seconds between the classic Macintosh epoch (1904-01-01 00:00:00)
 * and the Unix epoch (1970-01-01 00:00:00).
 */
#define RTC_MAC_EPOCH_OFFSET    INT64_C(2082844800)

#define RTC_PRAM_SIZE           256

/* Injectable host-time provider.  get_unix_seconds() returns the
 * current host wall-clock time in seconds since the Unix epoch.  A
 * NULL provider or NULL function behaves as if the host clock is 0.
 */
struct rtc_time_provider {
        int64_t (*get_unix_seconds)(void *ctx);
        void *ctx;
};

/* Transient serial decoder state (reset on deselect / warm reset). */
enum rtc_state {
        RTC_DESELECTED = 0,
        RTC_RX_CMD,             /* receiving first command byte */
        RTC_RX_CMD2,            /* receiving extended-address byte */
        RTC_RX_DATA,            /* receiving a write data byte */
        RTC_TX_DATA,            /* transmitting a read data byte */
        RTC_COMPLETE,           /* byte(s) done; awaiting deselect */
};

typedef struct rtc {
        /* Non-volatile state (survives warm reset; persisted). */
        uint8_t pram[RTC_PRAM_SIZE];
        /* Classic-epoch seconds = host_unix_seconds + clock_offset.
         * Default offset is RTC_MAC_EPOCH_OFFSET (no guest adjustment).
         */
        int64_t clock_offset;
        int write_protect;

        /* Host time provider. */
        struct rtc_time_provider tp;

        /* Transient serial / pin state. */
        enum rtc_state state;
        int prev_enb;
        int prev_clk;
        int data_out;           /* bit currently driven onto PB0 */

        int rx_count;           /* bits received into rx_byte */
        uint8_t rx_byte;

        int tx_bit;             /* next bit index (7..0) to drive */
        uint8_t tx_byte;

        /* Decoded command context. */
        int cmd_read;           /* 1 = read transaction */
        int cmd_ext;            /* 1 = extended PRAM command */
        int ext_sector;         /* sector (0..7) from first ext byte */
} rtc_t;

/* Cold initialisation: zero PRAM, default clock offset, clear state. */
void    rtc_init(rtc_t *r, const struct rtc_time_provider *tp);

/* Warm reset: clears only the transient serial state machine.  PRAM
 * and the clock offset are preserved (battery-backed).
 */
void    rtc_reset(rtc_t *r);

/* Update the RTC from resolved VIA Port B pin levels.  enb/clk/data
 * are the pin logic levels (0/1).  data_is_output is 1 when the VIA
 * drives PB0 (DDRB bit 0 = output) and 0 when PB0 is an input the RTC
 * may drive.
 */
void    rtc_via_update(rtc_t *r, int enb, int clk, int data,
                       int data_is_output);

/* Current RTC data bit on PB0 (meaningful when PB0 is an input). */
int     rtc_data_out(rtc_t *r);

/* Current 32-bit classic-epoch seconds counter. */
uint32_t rtc_get_seconds(rtc_t *r);

/* Persistence.  The backing file stores all 256 PRAM bytes plus the
 * guest clock offset.  Returns 0 on success, non-zero on error.
 * rtc_pram_load() leaves the RTC at cold defaults on a missing or
 * invalid/corrupt file and returns non-zero in that case.
 */
int     rtc_pram_save(rtc_t *r, const char *path);
int     rtc_pram_load(rtc_t *r, const char *path);

#endif
