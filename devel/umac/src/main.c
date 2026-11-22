/* umac
 *
 * Micro Mac 128K emulator
 *
 * Main file with:
 * - umac_ entry points,
 * - main loop,
 * - address decoding/memory map, dispatch to VIA/SCC/IWM/SCSI/disc
 * - Keyboard/mouse event dispatch
 *
 * Copyright 2024 Matt Evans
 *
 * Small portions of m68k interrupt code, error handling taken from
 * Musashi, which is Copyright 1998-2002 Karl Stenerud.
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
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <time.h>

#include "machw.h"
#include "m68k.h"
#include "m68681_emulator.h"
#include "via.h"
#include "scc.h"
#include "iwm.h"
#include "umac.h"
#include "scsi5380.h"
#include "rtc.h"

#ifdef PICO
#include "pico.h"
#define FAST_FUNC(x)    __not_in_flash_func(x)
#else
#define FAST_FUNC(x)    x
#endif


#ifdef DEBUG
#define MDBG(...)       printf(__VA_ARGS__)
#else
#define MDBG(...)       do {} while(0)
#endif

#define MERR(...)       fprintf(stderr, __VA_ARGS__)

/* Data */
static unsigned int g_int_controller_pending = 0;      /* list of pending interrupts */
static unsigned int g_int_controller_highest_int = 0;  /* Highest pending interrupt */

uint8_t *_ram_base;
const uint8_t *_rom_base;

int overlay = 1;
static int sim_done = 0;
static jmp_buf main_loop_jb;
static int disassemble = 0;

#define UMAC_QUANTUM_TICKS      (MAC_C8M_HZ / 200) /* 5 ms */

static struct iwm iwm;
static FILE *iwm_trace_stream;
static FILE *debug_uart_stream;
static unsigned int last_trace_sense;
static uint64_t machine_ticks, bus_ticks, via_bus_ticks;
static uint64_t via_irq_at = UINT64_MAX;
static bool cpu_running;

static void    update_overlay_layout(void);

enum bus_device { BUS_RAM, BUS_GRID, BUS_VIA, BUS_SCC, BUS_OTHER };

/* Musashi deducts the instruction's nominal cost after the last memory
 * callback. Place its ordered bus transfers in 4-clock slots from the
 * instruction hook, then charge extra waits to Musashi's elapsed time.
 * The Plus TSM PAL phase model is an approximation of bus completion. */
static uint64_t bus_access(unsigned int words, enum bus_device device)
{
        if (!cpu_running)
                return machine_ticks;

        unsigned int waits = 0;
        while (words--) {
                unsigned int phase = bus_ticks & 7;
                unsigned int wait = 0;
                switch (device) {
                case BUS_RAM: {
                        unsigned int raster = bus_ticks % MAC_FRAME_TICKS;
                        unsigned int x = raster % MAC_LINE_TICKS;
                        bool video = raster >= 28 * MAC_LINE_TICKS && x < 256;
                        wait = (video || x >= 344) ? (5u - phase) & 7 : (1u - phase) & 3;
                        break;
                }
                case BUS_GRID: wait = (1u - phase) & 3; break;
                case BUS_VIA:  wait = 6 + (10 - bus_ticks % MAC_VIA_DIVISOR) % MAC_VIA_DIVISOR; break;
                case BUS_SCC:  wait = 1 + ((4u - phase) & 7); break;
                case BUS_OTHER: break;
                }
                bus_ticks += 4 + wait;
                waits += wait;
        }
        if (waits)
                m68k_add_wait_cycles(waits);
        return bus_ticks - 1;
}

static void trace_iwm(char op, unsigned int address, uint8_t value, uint8_t old, uint64_t ticks)
{
        unsigned int reg = iwm_drive_register(&iwm);
        unsigned int q = iwm.switches & (IWM_Q6 | IWM_Q7);
        bool sense = op == 'R' && q == IWM_Q6;
        unsigned int sample = (reg << 8) | value;
        bool mark = op == 'R' && !q && (value == 0xd5 || value == 0xaa);
        if (op == 'R' && old == iwm.switches && !mark && !(sense && sample != last_trace_sense))
                return;
        if (sense)
                last_trace_sense = sample;

        struct disk_img_status drive = disk_img_status(iwm.internal);
        uint8_t enables = iwm_drive_enables(&iwm);
        uint64_t cpu_ticks = cpu_running ? machine_ticks + m68k_cycles_run() : machine_ticks;
        fprintf(iwm_trace_stream,
                "IWM t=%" PRIu64 " cpu_t=%" PRIu64 " pc=%06x %c %06x=%02x q6=%u q7=%u ph=%x sel=%u"
                " drive=%u enbl=%u/%u motor=%u off_at=%" PRIu64 " reg=%u cmd=%d ca2=%u mode=%02x"
                " dir=%u trk=%u side=%u spindle=%u step=%" PRIu64 " ready=%" PRIu64
                " ready_n=%u tach=%u media=%u bit=%u latch=%02x"
                " write_bits=%u write_result=%u write_at=%" PRIu64 " pending=%u underrun=%u dirty=%x provisional=%x save_error=%d\n",
                ticks, cpu_ticks, m68k_get_reg(NULL, M68K_REG_PPC), op, address, value,
                !!(q & IWM_Q6), !!(q & IWM_Q7), iwm.switches & 15, iwm.sel,
                !!(iwm.switches & IWM_EXTERNAL), !(enables & IWM_ENBL1), !(enables & IWM_ENBL2),
                !!(iwm.switches & IWM_MOTOR), iwm.motor_off_at, reg,
                (enables & IWM_ENBL1) && !(old & IWM_STROBE) && (iwm.switches & IWM_STROBE)
                        ? (int)(reg & 7) : -1,
                !!(iwm.switches & IWM_PHASE2), iwm.mode, drive.step_out, drive.cylinder, drive.side,
                drive.spindle, drive.step_done_at, drive.ready_at, disk_img_sense(iwm.internal, 13),
                disk_img_sense(iwm.internal, 7), !disk_img_sense(iwm.internal, 1),
                drive.bit_position, iwm.latch, drive.write_bits, drive.write_result, iwm.write_at,
                iwm.write_pending, iwm.write_underrun, drive.dirty_sides, drive.provisional_sides, drive.checkpoint_error);
}

void umac_iwm_trace(FILE *stream)
{
        iwm_trace_stream = stream;
        last_trace_sense = UINT_MAX;
}

////////////////////////////////////////////////////////////////////////////////

static int      m68k_dump_regs(char *buf, int len)
{
        int r;
        int orig_len = len;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "D%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_D0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        for (int i = 0; i < 8; i++) {
                r = snprintf(buf, len, "A%d: %08x  ", i, m68k_get_reg(NULL, M68K_REG_A0 + i));
                buf += r;
                len -= r;
        }
        *buf++ = '\n';
        len--;
        r = snprintf(buf, len, "SR: %08x  SP: %08x USP: %08x ISP: %08x MSP: %08x\n",
                     m68k_get_reg(NULL, M68K_REG_SR),
                     m68k_get_reg(NULL, M68K_REG_SP),
                     m68k_get_reg(NULL, M68K_REG_USP),
                     m68k_get_reg(NULL, M68K_REG_ISP),
                     m68k_get_reg(NULL, M68K_REG_MSP));
        return orig_len - len;
}

/* Exit with an error message.  Use printf syntax. */
void exit_error(char* fmt, ...)
{
	static int guard_val = 0;
	char buff[500];
	unsigned int pc;
	va_list args;

	if(guard_val)
		return;
	else
		guard_val = 1;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fprintf(stderr, "\n");
	pc = m68k_get_reg(NULL, M68K_REG_PPC);
	m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	fprintf(stderr, "At %04x: %s\n", pc, buff);

        m68k_dump_regs(buff, 500);
        fprintf(stderr, "%s", buff);
        sim_done = 1;
        longjmp(main_loop_jb, 1);
}


////////////////////////////////////////////////////////////////////////////////
// RTC (serial real-time clock + battery-backed PRAM, behind VIA Port B)

static rtc_t rtc;

static int64_t rtc_host_time_cb(void *ctx)
{
        (void)ctx;
        return (int64_t)time(NULL);
}

/* Resolved Port B pins: PB2=rtcEnb, PB1=rtcClk, PB0=rtcData.
 * DDRB bit 0 selects whether the VIA drives PB0 (output) or the RTC
 * may drive it (input). */
static void     via_rtc_pins(uint8_t orb, uint8_t ddrb)
{
        int enb = (orb >> 2) & 1;
        int clk = (orb >> 1) & 1;
        int data = orb & 1;
        int data_is_output = ddrb & 1;
        rtc_via_update(&rtc, enb, clk, data, data_is_output);
}

/* Persistence hooks (used by the Unix front-end). */
int     umac_rtc_load(const char *path)
{
        return rtc_pram_load(&rtc, path);
}

int     umac_rtc_save(const char *path)
{
        return rtc_pram_save(&rtc, path);
}

////////////////////////////////////////////////////////////////////////////////
// VIA-related controls
static void     via_ra_changed(uint8_t val)
{
        // 7 = scc w/req,a,b (in, indicates RX pending, w/o IRQ)
        // 6 = vid.pg2 (screen buffer select)
        // 5 = hd.sel (SEL line, select head)
        // 4 = overlay
        // 3 = snd.pg2 (sound buffer select)
        // [2:0] = sound volume
        if (overlay != !!(val & 0x10)) {
                overlay = !!(val & 0x10);
                MDBG("OVERLAY CHANGING\n");
                update_overlay_layout();
        }
}

static void     via_ra_pins(uint8_t ora, uint8_t ddra)
{
        bool sel = (ora & ddra & 0x20) != 0;
        if (sel != iwm.sel)
                iwm_set_sel(&iwm, sel, via_bus_ticks);
}

static void     via_rb_changed(uint8_t val)
{
        // 7 = sndres (sound enable/disable)
        // 6 = hblank
        // 5 = mouse8 (in, mouse Y2)
        // 4 = mouse4 (in, mouse X2)
        // 3 = mouse7 (in, 0 = button pressed)
        // [2:0] = RTC controls
        (void)val;
}

static uint8_t  via_ra_in(void)
{
        /* SCC W/REQ is high (no RX pending). The SCC has no RX source. */
        return 0x80;
}

// Quadrature bits read from B[5:4] (Y=5, X=4)
static uint8_t via_quadbits = 0;
static uint8_t via_mouse_pressed = 0;

static uint8_t  via_rb_in(void)
{
        uint8_t v = via_quadbits;
        // Mouse not pressed!
        if (!via_mouse_pressed)
                v |= (1 << 3);
        // PB0 = RTC serial data (used by VIA only when PB0 is input)
        v = (v & ~0x01) | (rtc_data_out(&rtc) & 1);
        return v;
}

/* Keyboard interface:
 *
 * Very roughly, it uses CB2 as bidirectional data and CB1 as clock
 * always from keyboard.  There's a handshake with the mac driving
 * data low as a "request to start clocking", with the kbd receiving a
 * byte (clocking out from SR) after that.  The mac does this by
 * "transmitting a byte" of all zeroes, which looks like pulling data
 * low.
 *
 * The VIA SR has a sequence of interrupts as follows:
 * - Mac pulls data low (transmits zero) then immediately loads SR
 *   with the data to TX (a command such as Inquiry)
 * - The VIA asserts SR IRQ when the command's transmitted (the kbd
 *   has woken and clocked it out).
 * - The keyboard -- some time later, importantly -- responds with
 *   a byte in SR, and VIA asserts SR IRQ again.
 *
 * The keyboard does nothing except for respond to commands from the
 * host (i.e. there's nothing proactively transmitted).
 */
#define KBD_CMD_GET_MODEL       0x16
#define KBD_CMD_INQUIRY         0x10
#define KBD_MODEL               5
#define KBD_RSP_NULL            0x7b

static int kbd_last_cmd = 0;
static uint64_t kbd_last_cmd_time = 0;

static void     via_sr_tx(uint8_t data)
{
        if (kbd_last_cmd) {
                MDBG("KBD: Oops, transmitting %02x whilst cmd %02x pending!\n",
                     data, kbd_last_cmd);
        }
        kbd_last_cmd = data;
        kbd_last_cmd_time = via_bus_ticks;
}

static int kbd_pending_evt = -1;
/* Emulate the keyboard: receive commands (such as an inquiry, polling
 * for keypresses) and respond using via_sr_rx().
 */
static void     kbd_rx(uint8_t data)
{
        /* Respond to requests with potted keyboard banter */
        switch (data) {
        case KBD_CMD_GET_MODEL:
                via_sr_rx(0x01 | (KBD_MODEL << 1));
                break;
        case KBD_CMD_INQUIRY:
                if (kbd_pending_evt == -1) {
                        via_sr_rx(KBD_RSP_NULL);
                } else {
                        via_sr_rx(kbd_pending_evt);
                        kbd_pending_evt = -1;
                }
        break;

        default:
                MERR("KBD: Unhandled TX %02x\n", data);
        }
}

static void     kbd_check_work(void)
{
        /* Process a keyboard command a little later than the transmit
         * time (i.e. not immediately, which makes the mac feel rushed
         * and causes it to ignore the response to punish our
         * hastiness).
         */
        if (kbd_last_cmd &&
            ((machine_ticks - kbd_last_cmd_time) > UMAC_QUANTUM_TICKS)) {
                MDBG("KBD: got cmd 0x%x\n", kbd_last_cmd);
                kbd_rx(kbd_last_cmd);
                kbd_last_cmd = 0;
        }
}

void    umac_kbd_event(uint8_t scancode, int down)
{
        if (kbd_pending_evt >= 0) {
                MDBG("KBD: Received event %02x with event %02x pending!\n",
                     scancode, kbd_pending_evt);
                /* FIXME: Add a queue */
        }
        kbd_pending_evt = scancode | (down ? 0 : 0x80);
}

// VIA IRQ output hook:
static void     via_irq_set(int status)
{
        MDBG("[IRQ: VIA IRQ %d]\n", status);
        if (status) {
                // IRQ is asserted
                m68k_set_virq(1, 1);
                if (cpu_running && m68k_cycles_remaining() > 0)
                        m68k_end_timeslice();
        } else {
                // IRQ de-asserted
                m68k_set_virq(1, 0);
        }
}

// Ditto, for SCC
static int scc_irq_state = 0;
static void     scc_irq_set(int status)
{
        MDBG("[IRQ: SCC IRQ %d]\n", status);
        if (status) {
                m68k_set_virq(2, 1);
        } else {
                m68k_set_virq(2, 0);
        }
        scc_irq_state = status;
}

////////////////////////////////////////////////////////////////////////////////
// IWM

static uint8_t read_iwm(unsigned int address)
{
        uint64_t ticks = bus_access(1, BUS_GRID);
        if (!(address & 1))
                return 0xff;
        uint8_t old = iwm.switches;
        uint8_t data = iwm_read(&iwm, address, ticks);
        if (iwm_trace_stream)
                trace_iwm('R', address, data, old, ticks);
        return data;
}

static void write_iwm(unsigned int address, uint8_t data)
{
        uint64_t ticks = bus_access(1, BUS_GRID);
        if (address & 1) {
                uint8_t old = iwm.switches;
                iwm_write(&iwm, address, data, ticks);
                if (iwm_trace_stream)
                        trace_iwm('W', address, data, old, ticks);
        }
}

////////////////////////////////////////////////////////////////////////////////
// Standalone ROM debug UART

void    umac_debug_uart(FILE *stream)
{
        m68681_emulator_reset();
        debug_uart_stream = stream;
}

static void drain_debug_uart(void)
{
        if (!debug_uart_stream)
                return;

        m68681_channel_t *uart_b = m68681_emulator_get_channel(M68681_EMULATOR_CHANNEL_B);
        if (m68681_emulator_tx_pending(uart_b)) {
                fputc(m68681_emulator_get_tx_byte(uart_b), debug_uart_stream);
                fflush(debug_uart_stream);
        }

        /* An unconnected transmitter still completes its transfer. */
        m68681_channel_t *uart_a = m68681_emulator_get_channel(M68681_EMULATOR_CHANNEL_A);
        if (m68681_emulator_tx_pending(uart_a))
                (void)m68681_emulator_get_tx_byte(uart_a);
}

////////////////////////////////////////////////////////////////////////////////

static inline bool rom_output_enabled(unsigned int address)
{
        /* A17 drives the Plus ROMs' active-low output enable. The ROM
         * compares the undriven $420000 and the mirror at $440000 to
         * tell a Plus with SCSI from an older machine. */
        return IS_ROM_WINDOW(address) && !(address & 0x020000);
}

static inline unsigned int rom_read_byte(unsigned int address)
{
        return rom_output_enabled(address) ? ROM_RD8(address & (ROM_SIZE - 1)) : 0xff;
}

static inline unsigned int rom_read_word(unsigned int address)
{
        if (address & 1)
                return (rom_read_byte(address) << 8) | rom_read_byte(address + 1);
        return rom_output_enabled(address) ? ROM_RD_ALIGNED_BE16(address & (ROM_SIZE - 1)) : 0xffff;
}

static inline unsigned int rom_read_long(unsigned int address)
{
        return (rom_read_word(address) << 16) | rom_read_word(address + 2);
}

static unsigned int  FAST_FUNC(cpu_read_instr_normal)(unsigned int address)
{
        /* Can check for 0x400000 (ROM) and otherwise RAM */
        if ((address & 0xf00000) != ROM_ADDR)
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
        else
                return rom_read_word(address);
}

static unsigned int  FAST_FUNC(cpu_read_instr_overlay)(unsigned int address)
{
        /* Need to check for both 0=ROM, 0x400000=ROM, and RAM at 0x600000...
         */
        if (IS_ROM_WINDOW(address))
                return rom_read_word(address);
        else /* RAM */
                return RAM_RD_ALIGNED_BE16(CLAMP_RAM_ADDR(address));
}

unsigned int (*cpu_read_instr)(unsigned int address) = cpu_read_instr_overlay;

/* Read data from RAM, ROM, or a device */
unsigned int    FAST_FUNC(cpu_read_byte)(unsigned int address)
{
        /* Most likely a RAM access, followed by a ROM access, then I/O */
        if (IS_RAM(address)) {
                bus_access(1, BUS_RAM);
                return RAM_RD8(CLAMP_RAM_ADDR(address));
        }
        if (IS_ROM_WINDOW(address)) {
                bus_access(1, BUS_GRID);
                return rom_read_byte(address);
        }

        // decode IO etc
        if (IS_VIA(address)) {
                via_bus_ticks = bus_access(1, BUS_VIA);
                via_advance(via_bus_ticks);
                uint8_t value = via_read(address);
                via_irq_at = via_next_timer_irq();
                return value;
        }
        if (IS_IWM(address))
                return read_iwm(address);
        if (IS_SCC_RD(address)) {
                bus_access(1, BUS_SCC);
                return scc_read(address);
        }
        if (IS_SCSI(address)) {
                bus_access(1, BUS_GRID);
                if (address & 1)
                        return 0;
                return SCSI_DACK(address) ? ncr5380_pseudo_dma_read()
                                          : ncr5380_read_register(SCSI_REG(address));
        }
        bus_access(1, BUS_OTHER);
        if (debug_uart_stream && IS_M68681(address))
                return m68681_emulator_read(ADR24(address) - M68681_BASE);
        if (IS_DUMMY(address))
                return 0;

        printf("Attempted to read byte from address %08x\n", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_word)(unsigned int address)
{
        if (IS_RAM(address)) {
                bus_access(1, BUS_RAM);
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        }
        if (IS_ROM_WINDOW(address)) {
                bus_access(1, BUS_GRID);
                return rom_read_word(address);
        }

        if (IS_IWM(address))
                return 0xff00 | read_iwm(address + 1);
        /* SCSI reads use the upper lane: one NCR access per 68000 word cycle. */
        if (IS_SCSI(address))
                return (address & 1) ? 0 : cpu_read_byte(address) << 8;
        bus_access(1, BUS_OTHER);
        if (debug_uart_stream && IS_M68681(address))
                return 0xffff;
        if (IS_TESTSW(address))
                return 0;

        exit_error("Attempted to read word from address %08x", address);
        return 0;
}

unsigned int    FAST_FUNC(cpu_read_long)(unsigned int address)
{
        if (IS_RAM(address)) {
                bus_access(2, BUS_RAM);
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        }
        if (IS_ROM_WINDOW(address)) {
                bus_access(2, BUS_GRID);
                return rom_read_long(address);
        }

        if (IS_IWM(address) && IS_IWM(address + 2)) {
                unsigned int high = cpu_read_word(address);
                return (high << 16) | cpu_read_word(address + 2);
        }
        if (IS_SCSI(address)) {
                if (address & 1)
                        return 0;
                unsigned int high = cpu_read_word(address);
                return (high << 16) | cpu_read_word(address + 2);
        }
        bus_access(2, BUS_OTHER);
        if (debug_uart_stream && IS_M68681(address))
                return 0xffffffff;
        if (IS_TESTSW(address))
                return 0;

        exit_error("Attempted to read long from address %08x", address);
        return 0;
}


unsigned int    cpu_read_word_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD16(CLAMP_RAM_ADDR(address));
        if (IS_ROM_WINDOW(address))
                return rom_read_word(address);

        exit_error("Disassembler attempted to read word from address %08x", address);
        return 0;
}

unsigned int    cpu_read_long_dasm(unsigned int address)
{
        if (IS_RAM(address))
                return RAM_RD32(CLAMP_RAM_ADDR(address));
        if (IS_ROM_WINDOW(address))
                return rom_read_long(address);

        exit_error("Dasm attempted to read long from address %08x", address);
        return 0;
}


/* Write data to RAM or a device */
void    FAST_FUNC(cpu_write_byte)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                bus_access(1, BUS_RAM);
                RAM_WR8(CLAMP_RAM_ADDR(address), value);
                return;
        }

        // decode IO
        if (IS_VIA(address)) {
                via_bus_ticks = bus_access(1, BUS_VIA);
                via_advance(via_bus_ticks);
                bool old_sel = iwm.sel;
                via_write(address, value);
                if (iwm_trace_stream && old_sel != iwm.sel)
                        trace_iwm('A', address, value, iwm.switches, via_bus_ticks);
                via_irq_at = via_next_timer_irq();
                return;
        }
        if (IS_IWM(address)) {
                write_iwm(address, value);
                return;
        }
        if (IS_SCC_WR(address)) {
                bus_access(1, BUS_SCC);
                scc_write(address, value);
                return;
        }
        if (IS_SCSI(address)) {
                bus_access(1, BUS_GRID);
                if (address & 1) {
                        if (SCSI_DACK(address))
                                ncr5380_pseudo_dma_write(value);
                        else
                                ncr5380_write_register(SCSI_REG(address), value);
                }
                return;
        }
        bus_access(1, BUS_OTHER);
        if (debug_uart_stream && IS_M68681(address)) {
                /* Yield after accepting a byte so TxRDY can recover promptly. */
                if (m68681_emulator_write(ADR24(address) - M68681_BASE, value) &&
                    cpu_running && m68k_cycles_remaining() > 0)
                        m68k_end_timeslice();
                return;
        }
        if (IS_DUMMY(address))
                return;
        printf("Ignoring write %02x to address %08x\n", value&0xff, address);
}

void    FAST_FUNC(cpu_write_word)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                bus_access(1, BUS_RAM);
                RAM_WR16(CLAMP_RAM_ADDR(address), value);
                return;
        }
        if (IS_IWM(address)) {
                write_iwm(address + 1, value);
                return;
        }
        if (IS_SCSI(address)) {
                if (!(address & 1))
                        cpu_write_byte(address + 1, value & 0xff);
                return;
        }
        bus_access(1, BUS_OTHER);
        if (debug_uart_stream && IS_M68681(address))
                return;
        printf("Ignoring write %04x to address %08x\n", value&0xffff, address);
}

void    FAST_FUNC(cpu_write_long)(unsigned int address, unsigned int value)
{
        if (IS_RAM(address)) {
                bus_access(2, BUS_RAM);
                RAM_WR32(CLAMP_RAM_ADDR(address), value);
                return;
        }
        if ((IS_IWM(address) && IS_IWM(address + 2)) || IS_SCSI(address)) {
                if (!(address & 1)) {
                        cpu_write_word(address, value >> 16);
                        cpu_write_word(address + 2, value & 0xffff);
                }
                return;
        }
        bus_access(2, BUS_OTHER);
        if (debug_uart_stream && IS_M68681(address))
                return;
        printf("Ignoring write %08x to address %08x\n", value, address);
}

/* Update function pointers for memory accessors based on overlay state/memory map layout */
static void     update_overlay_layout(void)
{
        if (overlay) {
                cpu_read_instr = cpu_read_instr_overlay;
        } else {
                cpu_read_instr = cpu_read_instr_normal;
        }
}

/* Called when the CPU pulses the RESET line */
void    cpu_pulse_reset(void)
{
        ncr5380_reset();
        m68681_emulator_reset();
        uint64_t ticks = cpu_running ? bus_ticks : machine_ticks;
        bool sel = iwm.sel;
        iwm_reset(&iwm, ticks);
        iwm_set_sel(&iwm, sel, ticks);
}

/* Called when the CPU acknowledges an interrupt */
int     cpu_irq_ack(int level)
{
        (void)level;
        /* Level really means line, so do an ack per device */
	return M68K_INT_ACK_AUTOVECTOR;
}

/* Implementation for the interrupt controller */
void    int_controller_set(unsigned int value)
{
	unsigned int old_pending = g_int_controller_pending;

	g_int_controller_pending |= (1<<value);

	if(old_pending != g_int_controller_pending && value > g_int_controller_highest_int)
	{
		g_int_controller_highest_int = value;
		m68k_set_irq(g_int_controller_highest_int);
	}
}

void    int_controller_clear(unsigned int value)
{
	g_int_controller_pending &= ~(1<<value);

	for(g_int_controller_highest_int = 7;g_int_controller_highest_int > 0;g_int_controller_highest_int--)
		if(g_int_controller_pending & (1<<g_int_controller_highest_int))
			break;

	m68k_set_irq(g_int_controller_highest_int);
}

/* Disassembler */
static void     make_hex(char* buff, unsigned int pc, unsigned int length)
{
	char* ptr = buff;

	for(;length>0;length -= 2)
	{
		sprintf(ptr, "%04x", cpu_read_word_dasm(pc));
		pc += 2;
		ptr += 4;
		if(length > 2)
			*ptr++ = ' ';
	}
}

void    cpu_instr_callback(int pc)
{
	static char buff[100];
	static char buff2[100];
	static unsigned int instr_size;

        if (cpu_running) {
                uint64_t start = machine_ticks + m68k_cycles_run();
                if (start > bus_ticks)
                        bus_ticks = start;
                if (bus_ticks >= via_irq_at) {
                        via_advance(bus_ticks);
                        via_irq_at = via_next_timer_irq();
                }
        }

        if (!disassemble)
                return;

	instr_size = m68k_disassemble(buff, pc, M68K_CPU_TYPE_68000);
	make_hex(buff2, pc, instr_size);
	MDBG("E %03x: %-20s: %s\n", pc, buff2, buff);
	fflush(stdout);
}

static void reset_via(void)
{
        struct via_cb vcb = { .ra_change = via_ra_changed,
                              .rb_change = via_rb_changed,
                              .ra_in = via_ra_in,
                              .rb_in = via_rb_in,
                              .sr_tx = via_sr_tx,
                              .irq_set = via_irq_set,
                              .rb_pins = via_rtc_pins,
                              .ra_pins = via_ra_pins,
        };
        via_init(&vcb);
        via_advance(machine_ticks);
        via_irq_at = UINT64_MAX;
        via_bus_ticks = machine_ticks;
        iwm_set_sel(&iwm, false, machine_ticks);
}

int     umac_init(void *ram_base, const void *rom_base, struct disk_img *internal_floppy)
{
        _ram_base = ram_base;
        _rom_base = rom_base;
        machine_ticks = bus_ticks = via_bus_ticks = 0;
        cpu_running = false;
        sim_done = 0;
        overlay = 1;
        update_overlay_layout();
        iwm_init(&iwm, internal_floppy, machine_ticks);
        umac_debug_uart(NULL);

        ncr5380_reset();
	m68k_init();
	m68k_set_cpu_type(M68K_CPU_TYPE_68000);
	m68k_pulse_reset();

        reset_via();
        struct rtc_time_provider rtp = { .get_unix_seconds = rtc_host_time_cb,
                                         .ctx = NULL };
        rtc_init(&rtc, &rtp);
        struct scc_cb scb = { .irq_set = scc_irq_set,
        };
        scc_init(&scb);
        return 0;
}

void    umac_opt_disassemble(int enable)
{
        disassemble = enable;
}

#define MOUSE_MAX_PENDING_PIX   30

static int pending_mouse_deltax = 0;
static int pending_mouse_deltay = 0;

/* Provide mouse input (movement, button) data.
 *
 * X is positive going right; Y is positive going upwards.
 */
void    umac_mouse(int deltax, int deltay, int button)
{
        pending_mouse_deltax += deltax;
        pending_mouse_deltay += deltay;

        /* Clamp if the UI has flooded with lots and lots of steps!
         */
        if (pending_mouse_deltax > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltax < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltax = -MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay > MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = MOUSE_MAX_PENDING_PIX;
        if (pending_mouse_deltay < -MOUSE_MAX_PENDING_PIX)
                pending_mouse_deltay = -MOUSE_MAX_PENDING_PIX;

        /* FIXME: The movement might take a little time, but this
         * posts the button status immediately.  Probably OK, but the
         * mismatch might be perceptible.
         */
        via_mouse_pressed = button;
}

static void     mouse_tick(void)
{
        /* Periodically, check if the mouse X/Y deltas are non-zero.
         * If a movement is required, encode one step in X and/or Y
         * and deduct from the pending delta.
         *
         * The step ultimately posts an SCC IRQ, so we _don't_ try to
         * make any more steps while an IRQ is currently pending.
         * (Currently, that means a previous step's DCD IRQ event
         * hasn't yet been consumed by the OS handler.  In future, if
         * SCC is extended with other IRQ types, then just checking
         * the IRQ status is technically too crude, but should still
         * be fine given the timeframes.)
         */
        if (pending_mouse_deltax == 0 && pending_mouse_deltay == 0)
                return;

        if (scc_irq_state == 1)
                return;

        static int old_dcd_a = 0;
        static int old_dcd_b = 0;

        /* Mouse X/Y quadrature signals are wired to:
         *  VIA Port B[4] & SCC DCD_A for X
         *  VIA Port B[5] & SCC DCD_B for Y
         *
         * As VIA mouse signals aren't sampled until IRQ, can do this
         * in one step, toggling existing DCD states and setting VIA
         * either equal or opposite to DCD:
         */
        int dcd_a = old_dcd_a;
        int dcd_b = old_dcd_b;
        int deltax = pending_mouse_deltax;
        int deltay = pending_mouse_deltay;
        uint8_t qb = via_quadbits;

        if (deltax) {
                dcd_a = !dcd_a;
                qb = (qb & ~0x10) | ((deltax < 0) == dcd_a ? 0x10 : 0);
                pending_mouse_deltax += (deltax > 0) ? -1 : 1;
                MDBG("  px %d, oldpx %d", pending_mouse_deltax, deltax);
        }

        if (deltay) {
                dcd_b = !dcd_b;
                qb = (qb & ~0x20) | ((deltay < 0) == dcd_b ? 0x20 : 0);
                pending_mouse_deltay += (deltay > 0) ? -1 : 1;
                MDBG("  py %d, oldpy %d", pending_mouse_deltay, deltay);
        }
        MDBG("\n");

        via_quadbits = qb;
        old_dcd_a = dcd_a;
        old_dcd_b = dcd_b;
        scc_set_dcd(dcd_a, dcd_b);
}

void    umac_reset(void)
{
        overlay = 1;
        update_overlay_layout();
        cpu_pulse_reset();
        rtc_reset(&rtc);
        m68k_pulse_reset();
        reset_via();
}

uint64_t umac_ticks(void)
{
        return machine_ticks;
}

/* Run the emulator for about a frame.
 * Returns 0 for not-done, 1 when an exit/done condition arises.
 */
int     umac_loop(void)
{
        if (sim_done || setjmp(main_loop_jb)) {
                cpu_running = false;
                return 1;
        }

        uint64_t end = machine_ticks + UMAC_QUANTUM_TICKS;
        do {
                uint64_t deadline = via_irq_at < end ? via_irq_at : end;
                bus_ticks = machine_ticks;
                cpu_running = true;
                int elapsed = m68k_execute(deadline - machine_ticks);
                cpu_running = false;
                machine_ticks += elapsed;
                if (bus_ticks > machine_ticks)
                        machine_ticks = bus_ticks;
                via_advance(machine_ticks);
                via_irq_at = via_next_timer_irq();
                drain_debug_uart();
        } while (machine_ticks < end);

        iwm_advance(&iwm, machine_ticks);
        mouse_tick();
        kbd_check_work();

	return sim_done;
}

