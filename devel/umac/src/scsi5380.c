#include "scsi5380.h"

enum {
        ICR_DATA = 0x01,
        ICR_ATN  = 0x02,
        ICR_SEL  = 0x04,
        ICR_BSY  = 0x08,
        ICR_ACK  = 0x10,
        ICR_AIP  = 0x40,         /* Read: arbitration in progress. Write: test mode. */
        ICR_TEST = 0x40,
        ICR_RST  = 0x80,
        ICR_SIGNALS = 0x9f,

        MODE_ARB = 0x01,
        MODE_DMA = 0x02,
        MODE_MONITOR_BSY = 0x04,
        MODE_TARGET = 0x40,

        CBS_PARITY = 0x01,
        CBS_SEL = 0x02,
        CBS_REQ = 0x20,
        CBS_BSY = 0x40,
        CBS_RST = 0x80,

        BSR_ACK = 0x01,
        BSR_ATN = 0x02,
        BSR_BSY_ERR = 0x04,
        BSR_MATCH = 0x08,
        BSR_IRQ = 0x10,
        BSR_DRQ = 0x40,

        MAC_SCSI_ID_BIT = 0x80,
};

enum dma_direction { DMA_NONE, DMA_SEND, DMA_RECEIVE };

static struct scsi_target target;
static struct {
        enum scsi_phase phase;
        enum dma_direction dma;
        uint8_t output, input, icr, mode, tcr, select_enable, interrupts;
        uint8_t pio_data;
        bool arbitration, selection_response, pio_acked;
} ncr = { .phase = SCSI_BUS_FREE };

static uint8_t initiator_signals(void)
{
        if (ncr.icr & ICR_TEST)
                return 0;
        if (ncr.mode & MODE_TARGET)
                return ncr.icr & (ICR_BSY | ICR_SEL | ICR_RST);
        return ncr.icr & ICR_SIGNALS;
}

static bool connected(void)
{
        return ncr.phase < SCSI_BUS_FREE;
}

static bool busy(void)
{
        return connected() || ncr.selection_response || ncr.arbitration ||
               (initiator_signals() & ICR_BSY);
}

static bool request(void)
{
        return connected() && !ncr.pio_acked;
}

static bool input_phase(void)
{
        return connected() && (ncr.phase & 1);
}

static uint8_t phase_bits(void)
{
        return connected() ? ncr.phase : 0;
}

static bool phase_match(void)
{
        return !(ncr.mode & MODE_TARGET) && phase_bits() == (ncr.tcr & 7);
}

static bool output_enabled(void)
{
        return (initiator_signals() & ICR_DATA) && !input_phase() &&
               (!connected() || phase_match());
}

static uint8_t bus_data(void)
{
        if (input_phase())
                return target.peek(target.ctx, ncr.phase);
        if (ncr.arbitration || output_enabled())
                return ncr.output;
        return 0;
}

static bool dma_receive_ready(void)
{
        return ncr.dma == DMA_RECEIVE && request() && phase_match() &&
               input_phase() && !(initiator_signals() & ICR_ACK);
}

static void reset_controller(bool bus_reset)
{
        ncr.phase = SCSI_BUS_FREE;
        ncr.dma = DMA_NONE;
        ncr.output = ncr.input = ncr.mode = ncr.tcr = ncr.select_enable = 0;
        ncr.arbitration = ncr.selection_response = ncr.pio_acked = false;
        ncr.icr = bus_reset ? ICR_RST : 0;
        ncr.interrupts = bus_reset ? BSR_IRQ : 0;
        if (target.reset)
                target.reset(target.ctx, bus_reset);
}

void ncr5380_reset(void)
{
        reset_controller(false);
}

void ncr5380_attach(const struct scsi_target *device)
{
        ncr5380_reset();
        target = device && device->id < 7 ? *device : (struct scsi_target){0};
}

static void update_selection(void)
{
        if (connected())
                return;
        uint8_t signals = initiator_signals();
        if (!(signals & ICR_SEL)) {
                if (ncr.selection_response) {
                        ncr.selection_response = false;
                        ncr.phase = SCSI_COMMAND;
                        target.reset(target.ctx, false);
                } else {
                        ncr.phase = SCSI_BUS_FREE;
                }
                return;
        }

        ncr.phase = SCSI_SELECTION;
        if (busy() || !(signals & ICR_DATA))
                return;
        if (ncr.output & ncr.select_enable)
                ncr.interrupts |= BSR_IRQ;
        if (target.transfer && ncr.output == (MAC_SCSI_ID_BIT | (1u << target.id)))
                ncr.selection_response = true;
        /* With no responder, SEL stays asserted until the guest times out. */
}

static void update_status(void)
{
        if ((ncr.mode & MODE_MONITOR_BSY) && !busy()) {
                ncr.interrupts |= BSR_BSY_ERR | BSR_IRQ;
                ncr.dma = DMA_NONE;
                ncr.mode &= ~MODE_DMA;
                ncr.icr &= ICR_RST;
                update_selection();
        }
        if ((ncr.mode & MODE_DMA) && request() && !(ncr.mode & MODE_TARGET) && !phase_match())
                ncr.interrupts |= BSR_IRQ;
        if (dma_receive_ready())
                ncr.input = bus_data();
}

static void complete_byte(uint8_t data)
{
        ncr.phase = target.transfer(target.ctx, ncr.phase, data);
        update_status();
}

static void write_icr(uint8_t data)
{
        if (data & ICR_RST) {
                if (!(ncr.icr & ICR_RST))
                        reset_controller(true);
                return;
        }

        uint8_t old = initiator_signals();
        bool was_requesting = request();
        ncr.icr = data & (ICR_SIGNALS | ICR_TEST);
        if (ncr.icr & ICR_TEST)
                ncr.arbitration = ncr.pio_acked = false;
        uint8_t signals = initiator_signals();
        if ((old ^ signals) & ICR_ACK) {
                if ((signals & ICR_ACK) && was_requesting && (input_phase() || output_enabled())) {
                        ncr.pio_data = bus_data();
                        ncr.pio_acked = true;    /* REQ drops until the guest drops ACK. */
                } else if (ncr.pio_acked && !(signals & ICR_ACK)) {
                        ncr.pio_acked = false;
                        complete_byte(ncr.pio_data);
                }
        }
        update_selection();
}

static uint8_t current_bus_status(void)
{
        uint8_t signals = initiator_signals();
        uint8_t status = phase_bits() << 2;
        if (busy())
                status |= CBS_BSY;
        if (request())
                status |= CBS_REQ;
        if (signals & ICR_RST)
                status |= CBS_RST;
        if (signals & ICR_SEL)
                status |= CBS_SEL;
        if (input_phase() || ncr.arbitration || output_enabled()) {
                /* The model always drives valid odd parity. */
                uint8_t bits = bus_data();
                bits ^= bits >> 4;
                bits ^= bits >> 2;
                bits ^= bits >> 1;
                if (!(bits & 1))
                        status |= CBS_PARITY;
        }
        return status;
}

static uint8_t bus_and_status(void)
{
        uint8_t status = ncr.interrupts;
        uint8_t signals = initiator_signals();
        if (signals & ICR_ACK)
                status |= BSR_ACK;
        if (signals & ICR_ATN)
                status |= BSR_ATN;
        if (phase_match())
                status |= BSR_MATCH;
        /* Send DRQ indicates an empty output latch. It stays ready after the
         * last byte, even though the next target REQ is for STATUS. */
        if ((ncr.dma == DMA_SEND && !(signals & ICR_ACK)) || dma_receive_ready())
                status |= BSR_DRQ;
        return status;
}

uint8_t ncr5380_read_register(unsigned int reg)
{
        switch (reg) {
        case 0:
                return bus_data();
        case 1:
                /* Lost arbitration cannot occur without another initiator. */
                return (ncr.icr & ICR_SIGNALS) | (ncr.arbitration ? ICR_AIP : 0);
        case 2:
                return ncr.mode;
        case 3:
                return ncr.tcr;
        case 4:
                return current_bus_status();
        case 5:
                return bus_and_status();
        case 6:
                return ncr.input;
        case 7:
                ncr.interrupts = 0;
                return 0;
        default:
                return 0;
        }
}

void ncr5380_write_register(unsigned int reg, uint8_t data)
{
        if ((ncr.icr & ICR_RST) && reg != 1)
                return;
        switch (reg) {
        case 0:
                ncr.output = data;
                update_selection();
                break;
        case 1:
                write_icr(data);
                break;
        case 2:
                if (!(data & MODE_ARB) || (data & MODE_TARGET)) {
                        ncr.arbitration = false;
                } else if (!(ncr.mode & MODE_ARB) && !busy() && ncr.phase == SCSI_BUS_FREE &&
                           !(ncr.icr & (ICR_TEST | ICR_SEL)) && ncr.output == MAC_SCSI_ID_BIT) {
                        ncr.arbitration = true;
                }
                if (!(data & MODE_DMA) || (data & MODE_TARGET))
                        ncr.dma = DMA_NONE;
                if (data & MODE_TARGET)
                        ncr.pio_acked = false;
                ncr.mode = data;
                update_selection();
                break;
        case 3:
                ncr.tcr = data & 0x0f;
                break;
        case 4:                 /* Write-only select enable mask. */
                ncr.select_enable = data;
                update_selection();
                break;
        case 5:
        case 7:
                if ((ncr.mode & (MODE_DMA | MODE_TARGET)) == MODE_DMA)
                        ncr.dma = reg == 5 ? DMA_SEND : DMA_RECEIVE;
                break;
        case 6:                 /* Start DMA target receive: no target mode on the Mac. */
        default:
                break;
        }
        update_status();
}

uint8_t ncr5380_pseudo_dma_read(void)
{
        uint8_t data = ncr.input;
        if (dma_receive_ready())
                complete_byte(data);
        return data;             /* A premature read does not wait or advance. */
}

void ncr5380_pseudo_dma_write(uint8_t data)
{
        if (ncr.dma == DMA_SEND && request() && output_enabled() && !(initiator_signals() & ICR_ACK)) {
                ncr.output = data;
                complete_byte(data);
        }
}
