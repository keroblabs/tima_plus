/* Port of the Kerob Labs MC68681 model in m68k_emulator (24ee077). */
#include "m68681_emulator.h"
#include <string.h>

#define M68681_REG_COUNT               16
#define M68681_RX_FIFO_SIZE            3

#define M68681_REG_MR_A                0x00
#define M68681_REG_SR_A_CSRA           0x01
#define M68681_REG_CR_A                0x02
#define M68681_REG_RHR_A_THR_A         0x03
#define M68681_REG_IPCR_ACR            0x04
#define M68681_REG_ISR_IMR             0x05
#define M68681_REG_CTUR                0x06
#define M68681_REG_CTLR                0x07
#define M68681_REG_MR_B                0x08
#define M68681_REG_SR_B_CSRB           0x09
#define M68681_REG_CR_B                0x0A
#define M68681_REG_RHR_B_THR_B         0x0B
#define M68681_REG_IVR                 0x0C
#define M68681_REG_IP_OPCR             0x0D
#define M68681_REG_START_OPSET         0x0E
#define M68681_REG_STOP_OPRESET        0x0F

#define M68681_SR_RX_READY              (1u << 0)
#define M68681_SR_FIFO_FULL             (1u << 1)
#define M68681_SR_TX_READY              (1u << 2)
#define M68681_SR_TX_EMPTY              (1u << 3)
#define M68681_SR_OVERRUN               (1u << 4)

#define M68681_ISR_TX_A                 (1u << 0)
#define M68681_ISR_RX_A                 (1u << 1)
#define M68681_ISR_BREAK_A              (1u << 2)
#define M68681_ISR_COUNTER_TIMER        (1u << 3)
#define M68681_ISR_TX_B                 (1u << 4)
#define M68681_ISR_RX_B                 (1u << 5)
#define M68681_ISR_BREAK_B              (1u << 6)
#define M68681_ISR_INPUT_CHANGE         (1u << 7)

#define M68681_CR_MISC_MASK             0x70
#define M68681_CR_RESET_MR              0x10
#define M68681_CR_RESET_RX              0x20
#define M68681_CR_RESET_TX              0x30
#define M68681_CR_RESET_ERROR           0x40
#define M68681_CR_TX_MASK               0x0C
#define M68681_CR_TX_ENABLE             0x04
#define M68681_CR_TX_DISABLE            0x08
#define M68681_CR_RX_MASK               0x03
#define M68681_CR_RX_ENABLE             0x01
#define M68681_CR_RX_DISABLE            0x02

#define M68681_MR1_RX_IRQ_FIFO_FULL     (1u << 6)
#define M68681_ACR_TIMER_MODE            (1u << 6)

#define M68681_RESET_IVR                 0x0F
#define M68681_INVALID_READ              0xFF

struct m68681_channel
{
    uint8_t mode[2];
    uint8_t mode_pointer;
    uint8_t clock_select;

    bool rx_enabled;
    bool tx_enabled;

    uint8_t rx_fifo[M68681_RX_FIFO_SIZE];
    uint8_t rx_head;
    uint8_t rx_tail;
    uint8_t rx_count;
    bool rx_overrun;

    uint8_t tx_holding;
    bool tx_pending;
};

typedef struct
{
    m68681_channel_t channel[M68681_EMULATOR_CHANNEL_COUNT];

    uint8_t acr;
    uint8_t imr;
    uint8_t ivr;
    uint8_t ipcr;
    uint8_t input_port;
    uint8_t opcr;
    uint8_t output_port;

    uint16_t timer_preload;
    uint16_t timer_counter;
    bool timer_running;
    bool timer_irq;
} m68681_data_t;

static m68681_data_t duart;

static uint8_t m68681_channel_status(const m68681_channel_t * channel)
{
    uint8_t status = 0;

    if (channel->rx_enabled && (channel->rx_count != 0))
    {
        status |= M68681_SR_RX_READY;
    }

    if (channel->rx_enabled && (channel->rx_count >= M68681_RX_FIFO_SIZE))
    {
        status |= M68681_SR_FIFO_FULL;
    }

    if (channel->tx_enabled && !channel->tx_pending)
    {
        status |= M68681_SR_TX_READY | M68681_SR_TX_EMPTY;
    }

    if (channel->rx_overrun)
    {
        status |= M68681_SR_OVERRUN;
    }

    return status;
}

static bool m68681_rx_interrupt_pending(const m68681_channel_t * channel)
{
    if (!channel->rx_enabled)
    {
        return false;
    }

    if ((channel->mode[0] & M68681_MR1_RX_IRQ_FIFO_FULL) != 0)
    {
        return channel->rx_count >= M68681_RX_FIFO_SIZE;
    }

    return channel->rx_count != 0;
}

static uint8_t m68681_interrupt_status(const m68681_data_t * device)
{
    uint8_t status = 0;
    const m68681_channel_t * channel_a = &device->channel[M68681_EMULATOR_CHANNEL_A];
    const m68681_channel_t * channel_b = &device->channel[M68681_EMULATOR_CHANNEL_B];

    if (channel_a->tx_enabled && !channel_a->tx_pending)
    {
        status |= M68681_ISR_TX_A;
    }
    if (m68681_rx_interrupt_pending(channel_a))
    {
        status |= M68681_ISR_RX_A;
    }

    if (device->timer_irq)
    {
        status |= M68681_ISR_COUNTER_TIMER;
    }

    if (channel_b->tx_enabled && !channel_b->tx_pending)
    {
        status |= M68681_ISR_TX_B;
    }
    if (m68681_rx_interrupt_pending(channel_b))
    {
        status |= M68681_ISR_RX_B;
    }

    /* Break and input-change sources are modeled as inactive for now. */
    (void)M68681_ISR_BREAK_A;
    (void)M68681_ISR_BREAK_B;
    (void)M68681_ISR_INPUT_CHANGE;

    return status;
}

static void m68681_reset_receiver(m68681_channel_t * channel)
{
    channel->rx_enabled = false;
    channel->rx_head = 0;
    channel->rx_tail = 0;
    channel->rx_count = 0;
    channel->rx_overrun = false;
}

static void m68681_reset_transmitter(m68681_channel_t * channel)
{
    channel->tx_enabled = false;
    channel->tx_pending = false;
    channel->tx_holding = 0;
}

static void m68681_command_write(m68681_channel_t * channel, uint8_t data)
{
    switch (data & M68681_CR_MISC_MASK)
    {
        case M68681_CR_RESET_MR:
            channel->mode_pointer = 0;
            break;

        case M68681_CR_RESET_RX:
            m68681_reset_receiver(channel);
            break;

        case M68681_CR_RESET_TX:
            m68681_reset_transmitter(channel);
            break;

        case M68681_CR_RESET_ERROR:
            channel->rx_overrun = false;
            break;

        default:
            break;
    }

    switch (data & M68681_CR_TX_MASK)
    {
        case M68681_CR_TX_ENABLE:
            channel->tx_enabled = true;
            break;

        case M68681_CR_TX_DISABLE:
            channel->tx_enabled = false;
            break;

        default:
            break;
    }

    switch (data & M68681_CR_RX_MASK)
    {
        case M68681_CR_RX_ENABLE:
            channel->rx_enabled = true;
            break;

        case M68681_CR_RX_DISABLE:
            channel->rx_enabled = false;
            break;

        default:
            break;
    }
}

static uint8_t m68681_mode_access(m68681_channel_t * channel, uint8_t data, bool write)
{
    uint8_t ret = channel->mode[channel->mode_pointer];

    if (write)
    {
        channel->mode[channel->mode_pointer] = data;
        ret = data;
    }

    /* Any MR1 access advances the independent channel pointer to MR2. */
    if (channel->mode_pointer == 0)
    {
        channel->mode_pointer = 1;
    }

    return ret;
}

static uint8_t m68681_receive_read(m68681_channel_t * channel)
{
    uint8_t data = 0;

    if (channel->rx_count != 0)
    {
        data = channel->rx_fifo[channel->rx_tail];
        channel->rx_tail = (uint8_t)((channel->rx_tail + 1u) % M68681_RX_FIFO_SIZE);
        channel->rx_count--;
    }

    return data;
}

static bool m68681_transmit_write(m68681_channel_t * channel, uint8_t data)
{
    if (channel->tx_enabled && !channel->tx_pending)
    {
        channel->tx_holding = data;
        channel->tx_pending = true;
        return true;
    }
    return false;
}

static uint8_t m68681_register_read(m68681_data_t * device, uint8_t reg)
{
    switch (reg)
    {
        case M68681_REG_MR_A:
            return m68681_mode_access(&device->channel[M68681_EMULATOR_CHANNEL_A], 0, false);

        case M68681_REG_SR_A_CSRA:
            return m68681_channel_status(&device->channel[M68681_EMULATOR_CHANNEL_A]);

        case M68681_REG_CR_A:
            return 0;

        case M68681_REG_RHR_A_THR_A:
            return m68681_receive_read(&device->channel[M68681_EMULATOR_CHANNEL_A]);

        case M68681_REG_IPCR_ACR:
        {
            uint8_t ret = device->ipcr;
            device->ipcr = 0;
            return ret;
        }

        case M68681_REG_ISR_IMR:
            return m68681_interrupt_status(device);

        case M68681_REG_CTUR:
            return (uint8_t)(device->timer_counter >> 8);

        case M68681_REG_CTLR:
            return (uint8_t)device->timer_counter;

        case M68681_REG_MR_B:
            return m68681_mode_access(&device->channel[M68681_EMULATOR_CHANNEL_B], 0, false);

        case M68681_REG_SR_B_CSRB:
            return m68681_channel_status(&device->channel[M68681_EMULATOR_CHANNEL_B]);

        case M68681_REG_CR_B:
            return 0;

        case M68681_REG_RHR_B_THR_B:
            return m68681_receive_read(&device->channel[M68681_EMULATOR_CHANNEL_B]);

        case M68681_REG_IVR:
            return device->ivr;

        case M68681_REG_IP_OPCR:
            return device->input_port;

        case M68681_REG_START_OPSET:
            device->timer_counter = device->timer_preload;
            device->timer_running = true;
            return 0;

        case M68681_REG_STOP_OPRESET:
            device->timer_irq = false;
            if ((device->acr & M68681_ACR_TIMER_MODE) == 0)
            {
                device->timer_running = false;
            }
            return 0;

        default:
            return M68681_INVALID_READ;
    }
}

static bool m68681_register_write(m68681_data_t * device, uint8_t reg, uint8_t data)
{
    switch (reg)
    {
        case M68681_REG_MR_A:
            (void)m68681_mode_access(&device->channel[M68681_EMULATOR_CHANNEL_A], data, true);
            break;

        case M68681_REG_SR_A_CSRA:
            device->channel[M68681_EMULATOR_CHANNEL_A].clock_select = data;
            break;

        case M68681_REG_CR_A:
            m68681_command_write(&device->channel[M68681_EMULATOR_CHANNEL_A], data);
            break;

        case M68681_REG_RHR_A_THR_A:
            return m68681_transmit_write(&device->channel[M68681_EMULATOR_CHANNEL_A], data);

        case M68681_REG_IPCR_ACR:
            device->acr = data;
            break;

        case M68681_REG_ISR_IMR:
            device->imr = data;
            break;

        case M68681_REG_CTUR:
            device->timer_preload = (uint16_t)((device->timer_preload & 0x00FFu) | ((uint16_t)data << 8));
            break;

        case M68681_REG_CTLR:
            device->timer_preload = (uint16_t)((device->timer_preload & 0xFF00u) | data);
            device->timer_counter = device->timer_preload;
            break;

        case M68681_REG_MR_B:
            (void)m68681_mode_access(&device->channel[M68681_EMULATOR_CHANNEL_B], data, true);
            break;

        case M68681_REG_SR_B_CSRB:
            device->channel[M68681_EMULATOR_CHANNEL_B].clock_select = data;
            break;

        case M68681_REG_CR_B:
            m68681_command_write(&device->channel[M68681_EMULATOR_CHANNEL_B], data);
            break;

        case M68681_REG_RHR_B_THR_B:
            return m68681_transmit_write(&device->channel[M68681_EMULATOR_CHANNEL_B], data);

        case M68681_REG_IVR:
            device->ivr = data;
            break;

        case M68681_REG_IP_OPCR:
            device->opcr = data;
            break;

        case M68681_REG_START_OPSET:
            device->output_port |= data;
            break;

        case M68681_REG_STOP_OPRESET:
            device->output_port &= (uint8_t)~data;
            break;

        default:
            break;
    }
    return false;
}

/* The 68000 uses the low byte lane for the 16 byte-wide registers. */
uint8_t m68681_emulator_read(unsigned int offset)
{
    unsigned int reg = offset >> 1;
    if (!(offset & 1u) || reg >= M68681_REG_COUNT)
        return M68681_INVALID_READ;
    return m68681_register_read(&duart, reg);
}

bool m68681_emulator_write(unsigned int offset, uint8_t data)
{
    unsigned int reg = offset >> 1;
    if (!(offset & 1u) || reg >= M68681_REG_COUNT)
        return false;
    return m68681_register_write(&duart, reg, data);
}

void m68681_emulator_reset(void)
{
    memset(&duart, 0, sizeof(duart));
    duart.ivr = M68681_RESET_IVR;
}

m68681_channel_t *m68681_emulator_get_channel(unsigned int channel)
{
    return channel < M68681_EMULATOR_CHANNEL_COUNT ? &duart.channel[channel] : NULL;
}

bool m68681_emulator_rx_can_accept(m68681_channel_t *channel)
{
    if (channel == NULL)
    {
        return false;
    }

    return channel->rx_enabled && channel->rx_count < M68681_RX_FIFO_SIZE;
}

bool m68681_emulator_send_rx_byte(m68681_channel_t *channel, uint8_t data)
{
    if ((channel == NULL) || (!channel->rx_enabled))
    {
        return false;
    }

    if (channel->rx_count >= M68681_RX_FIFO_SIZE)
    {
        channel->rx_overrun = true;
        return false;
    }

    channel->rx_fifo[channel->rx_head] = data;
    channel->rx_head = (uint8_t)((channel->rx_head + 1u) % M68681_RX_FIFO_SIZE);
    channel->rx_count++;
    return true;
}

bool m68681_emulator_tx_pending(m68681_channel_t *channel)
{
    return channel != NULL && channel->tx_pending;
}

uint8_t m68681_emulator_get_tx_byte(m68681_channel_t *channel)
{
    uint8_t data = 0;

    if ((channel != NULL) && channel->tx_pending)
    {
        data = channel->tx_holding;
        channel->tx_pending = false;
    }

    return data;
}

bool m68681_emulator_irq_pending(void)
{
    return (m68681_interrupt_status(&duart) & duart.imr) != 0;
}

uint8_t m68681_emulator_interrupt_vector(void)
{
    return duart.ivr;
}
