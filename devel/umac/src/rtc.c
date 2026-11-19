/* umac Macintosh Plus serial RTC + battery-backed PRAM
 *
 * See include/rtc.h for the interface and the component reference
 * docs/tasks/db/343-0042-B-RTC-PRAM-reference.md for the protocol.
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

#include <stdio.h>
#include <string.h>

#include "rtc.h"

#ifdef RTC_DEBUG
#define RDBG(...)       printf(__VA_ARGS__)
#else
#define RDBG(...)       do {} while (0)
#endif

/* Persistence file format: magic, version, clock offset, 256 PRAM. */
#define RTC_FILE_MAGIC          "uRTC"
#define RTC_FILE_VERSION        1
/* 4 magic + 4 version + 8 offset + 256 PRAM */
#define RTC_FILE_SIZE           (4 + 4 + 8 + RTC_PRAM_SIZE)

/* Decoded command targets. */
enum rtc_target_kind {
        TGT_NONE = 0,
        TGT_SECONDS,            /* index = seconds byte 0..3 */
        TGT_PRAM,               /* index = PRAM offset 0..255 */
        TGT_WRITE_PROTECT,      /* write-protect control register */
        TGT_TEST,               /* diagnostic test register */
};

static int64_t rtc_host_seconds(rtc_t *r)
{
        if (r->tp.get_unix_seconds)
                return r->tp.get_unix_seconds(r->tp.ctx);
        return 0;
}

uint32_t rtc_get_seconds(rtc_t *r)
{
        int64_t t = rtc_host_seconds(r) + r->clock_offset;
        return (uint32_t)t;
}

static void rtc_set_seconds(rtc_t *r, uint32_t value)
{
        /* Anchor the guest-set value to the current host time so the
         * counter continues ticking from here without being clobbered
         * by the next host-time poll.
         */
        r->clock_offset = (int64_t)value - rtc_host_seconds(r);
}

void    rtc_init(rtc_t *r, const struct rtc_time_provider *tp)
{
        memset(r, 0, sizeof(*r));
        if (tp)
                r->tp = *tp;
        r->clock_offset = RTC_MAC_EPOCH_OFFSET;
        r->write_protect = 0;
        rtc_reset(r);
}

void    rtc_reset(rtc_t *r)
{
        r->state = RTC_DESELECTED;
        r->prev_enb = 1;        /* idle: deselected (active low) */
        r->prev_clk = 0;
        r->data_out = 1;
        r->rx_count = 0;
        r->rx_byte = 0;
        r->tx_bit = 7;
        r->tx_byte = 0;
        r->cmd_read = 0;
        r->cmd_ext = 0;
        r->ext_sector = 0;
}

int     rtc_data_out(rtc_t *r)
{
        return r->data_out & 1;
}

/* Decode a regular (one-byte) command's register index into a target. */
static enum rtc_target_kind rtc_decode_index(int index, int *out_index)
{
        if (index <= 0x03) {
                *out_index = index;             /* seconds byte 0..3 */
                return TGT_SECONDS;
        } else if (index <= 0x07) {
                *out_index = index - 0x04;      /* alias of seconds 0..3 */
                return TGT_SECONDS;
        } else if (index <= 0x0B) {
                *out_index = 0x10 + (index - 0x08); /* PRAM 0x10..0x13 */
                return TGT_PRAM;
        } else if (index == 0x0C) {
                *out_index = 0;
                return TGT_TEST;
        } else if (index == 0x0D) {
                *out_index = 0;
                return TGT_WRITE_PROTECT;
        } else if (index <= 0x0F) {
                /* 0x0E/0x0F: extended-family prefix, handled earlier. */
                *out_index = 0;
                return TGT_NONE;
        }
        /* 0x10..0x1F -> PRAM 0x00..0x0F */
        *out_index = index - 0x10;
        return TGT_PRAM;
}

/* Prepare the read data byte (TX_DATA) for the decoded target. */
static void rtc_begin_read(rtc_t *r, enum rtc_target_kind kind, int index)
{
        uint8_t v = 0;
        switch (kind) {
        case TGT_SECONDS:
                v = (uint8_t)(rtc_get_seconds(r) >> (8 * index));
                break;
        case TGT_PRAM:
                v = r->pram[index & 0xff];
                break;
        case TGT_WRITE_PROTECT:
                v = r->write_protect ? 0x80 : 0x00;
                break;
        case TGT_TEST:
        case TGT_NONE:
        default:
                v = 0;
                break;
        }
        r->tx_byte = v;
        r->tx_bit = 7;
        r->data_out = (v >> 7) & 1;
        r->state = RTC_TX_DATA;
        RDBG("[RTC: begin read kind=%d idx=%02x val=%02x]\n", kind, index, v);
}

/* Apply a received write data byte to the decoded target. */
static void rtc_apply_write(rtc_t *r, enum rtc_target_kind kind, int index,
                            uint8_t value)
{
        /* Write protection: only the write-protect register itself may
         * be changed while protected.  Reads are unaffected.
         */
        if (r->write_protect && kind != TGT_WRITE_PROTECT) {
                RDBG("[RTC: write blocked (protected) kind=%d]\n", kind);
                return;
        }

        switch (kind) {
        case TGT_SECONDS: {
                uint32_t s = rtc_get_seconds(r);
                uint32_t shift = 8u * (unsigned)index;
                s = (s & ~(0xffu << shift)) | ((uint32_t)value << shift);
                rtc_set_seconds(r, s);
                break;
        }
        case TGT_PRAM:
                r->pram[index & 0xff] = value;
                break;
        case TGT_WRITE_PROTECT:
                r->write_protect = (value & 0x80) ? 1 : 0;
                break;
        case TGT_TEST:
                /* Diagnostic test register: no software-observable
                 * behaviour is emulated (documented unimplemented). */
                break;
        case TGT_NONE:
        default:
                break;
        }
        RDBG("[RTC: write kind=%d idx=%02x val=%02x]\n", kind, index, value);
}

/* Process the first command byte. */
static void rtc_process_cmd(rtc_t *r, uint8_t cmd)
{
        int index = (cmd >> 2) & 0x1f;
        r->cmd_read = (cmd & 0x80) ? 1 : 0;

        if (index == 0x0E || index == 0x0F) {
                /* Extended PRAM command: sector in low 3 bits, a
                 * second address byte follows. */
                r->cmd_ext = 1;
                r->ext_sector = cmd & 0x07;
                r->state = RTC_RX_CMD2;
                r->rx_count = 0;
                r->rx_byte = 0;
                return;
        }

        r->cmd_ext = 0;
        int tgt_index = 0;
        enum rtc_target_kind kind = rtc_decode_index(index, &tgt_index);
        if (r->cmd_read) {
                rtc_begin_read(r, kind, tgt_index);
        } else {
                /* Stash target for the data byte to come. */
                r->ext_sector = (int)kind;       /* reuse as kind store */
                r->tx_byte = (uint8_t)tgt_index; /* reuse as index store */
                r->state = RTC_RX_DATA;
                r->rx_count = 0;
                r->rx_byte = 0;
        }
}

/* Process the extended-address second command byte. */
static void rtc_process_cmd2(rtc_t *r, uint8_t arg)
{
        int address = ((r->ext_sector & 0x07) << 5) | ((arg >> 2) & 0x1f);
        if (r->cmd_read) {
                rtc_begin_read(r, TGT_PRAM, address);
        } else {
                r->ext_sector = (int)TGT_PRAM;
                r->tx_byte = (uint8_t)address;  /* low 8 bits of address */
                /* address only spans 0..255 so one byte suffices */
                r->rx_count = 0;
                r->rx_byte = 0;
                r->state = RTC_RX_DATA;
        }
}

/* A write data byte has been fully received. */
static void rtc_process_data(rtc_t *r, uint8_t value)
{
        enum rtc_target_kind kind = (enum rtc_target_kind)r->ext_sector;
        int index = r->tx_byte;
        rtc_apply_write(r, kind, index, value);
        r->state = RTC_COMPLETE;
}

/* Handle one serial clock falling edge while selected. */
static void rtc_clock_edge(rtc_t *r, int data)
{
        switch (r->state) {
        case RTC_RX_CMD:
        case RTC_RX_CMD2:
        case RTC_RX_DATA:
                r->rx_byte = (uint8_t)((r->rx_byte << 1) | (data & 1));
                if (++r->rx_count >= 8) {
                        uint8_t b = r->rx_byte;
                        enum rtc_state s = r->state;
                        r->rx_count = 0;
                        r->rx_byte = 0;
                        if (s == RTC_RX_CMD)
                                rtc_process_cmd(r, b);
                        else if (s == RTC_RX_CMD2)
                                rtc_process_cmd2(r, b);
                        else
                                rtc_process_data(r, b);
                }
                break;

        case RTC_TX_DATA:
                /* The current bit was presented before this edge;
                 * advance to the next, MSB first. */
                if (r->tx_bit > 0) {
                        r->tx_bit--;
                        r->data_out = (r->tx_byte >> r->tx_bit) & 1;
                } else {
                        r->state = RTC_COMPLETE;
                }
                break;

        case RTC_DESELECTED:
        case RTC_COMPLETE:
        default:
                break;
        }
}

void    rtc_via_update(rtc_t *r, int enb, int clk, int data,
                       int data_is_output)
{
        (void)data_is_output;
        enb = enb ? 1 : 0;
        clk = clk ? 1 : 0;

        if (enb) {
                /* Deselected (active low high): abort any transfer and
                 * reset the transient shifter. */
                if (!r->prev_enb) {
                        r->state = RTC_DESELECTED;
                        r->data_out = 1;
                }
                r->prev_enb = enb;
                r->prev_clk = clk;
                return;
        }

        /* Selected (enb low). */
        if (r->prev_enb) {
                /* Falling edge of chip-enable: start a new command. */
                r->state = RTC_RX_CMD;
                r->rx_count = 0;
                r->rx_byte = 0;
                r->cmd_ext = 0;
        }

        /* Sample / advance on the clock falling edge. */
        if (r->prev_clk && !clk)
                rtc_clock_edge(r, data);

        r->prev_enb = enb;
        r->prev_clk = clk;
}

/**********************************************************************/
/* Persistence                                                        */

static void rtc_put_u32(uint8_t *p, uint32_t v)
{
        p[0] = (uint8_t)(v & 0xff);
        p[1] = (uint8_t)((v >> 8) & 0xff);
        p[2] = (uint8_t)((v >> 16) & 0xff);
        p[3] = (uint8_t)((v >> 24) & 0xff);
}

static uint32_t rtc_get_u32(const uint8_t *p)
{
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void rtc_put_i64(uint8_t *p, int64_t v)
{
        uint64_t u = (uint64_t)v;
        for (int i = 0; i < 8; i++)
                p[i] = (uint8_t)((u >> (8 * i)) & 0xff);
}

static int64_t rtc_get_i64(const uint8_t *p)
{
        uint64_t u = 0;
        for (int i = 0; i < 8; i++)
                u |= (uint64_t)p[i] << (8 * i);
        return (int64_t)u;
}

int     rtc_pram_save(rtc_t *r, const char *path)
{
        if (!path)
                return -1;

        uint8_t buf[RTC_FILE_SIZE];
        memcpy(buf, RTC_FILE_MAGIC, 4);
        rtc_put_u32(buf + 4, RTC_FILE_VERSION);
        rtc_put_i64(buf + 8, r->clock_offset);
        memcpy(buf + 16, r->pram, RTC_PRAM_SIZE);

        /* Atomic replace: write to a temp file, then rename. */
        char tmp[1024];
        int n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
        if (n <= 0 || (size_t)n >= sizeof(tmp))
                return -1;

        FILE *f = fopen(tmp, "wb");
        if (!f)
                return -1;
        int ok = (fwrite(buf, 1, sizeof(buf), f) == sizeof(buf));
        if (fflush(f) != 0)
                ok = 0;
        if (fclose(f) != 0)
                ok = 0;
        if (!ok) {
                remove(tmp);
                return -1;
        }
        if (rename(tmp, path) != 0) {
                remove(tmp);
                return -1;
        }
        return 0;
}

int     rtc_pram_load(rtc_t *r, const char *path)
{
        if (!path)
                return -1;

        FILE *f = fopen(path, "rb");
        if (!f)
                return -1;

        uint8_t buf[RTC_FILE_SIZE];
        size_t got = fread(buf, 1, sizeof(buf), f);
        /* Reject trailing garbage too. */
        int extra = (fgetc(f) != EOF);
        fclose(f);

        if (got != sizeof(buf) || extra)
                return -1;
        if (memcmp(buf, RTC_FILE_MAGIC, 4) != 0)
                return -1;
        if (rtc_get_u32(buf + 4) != RTC_FILE_VERSION)
                return -1;

        r->clock_offset = rtc_get_i64(buf + 8);
        memcpy(r->pram, buf + 16, RTC_PRAM_SIZE);
        return 0;
}
