#ifndef M68681_EMULATOR_H
#define M68681_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

enum {
    M68681_EMULATOR_CHANNEL_A,
    M68681_EMULATOR_CHANNEL_B,
    M68681_EMULATOR_CHANNEL_COUNT
};

typedef struct m68681_channel m68681_channel_t;

void m68681_emulator_reset(void);
uint8_t m68681_emulator_read(unsigned int offset);
/* Returns true when the write latches a transmit byte. */
bool m68681_emulator_write(unsigned int offset, uint8_t data);

m68681_channel_t *m68681_emulator_get_channel(unsigned int channel);
bool m68681_emulator_rx_can_accept(m68681_channel_t *channel);
bool m68681_emulator_send_rx_byte(m68681_channel_t *channel, uint8_t data);
bool m68681_emulator_tx_pending(m68681_channel_t *channel);
uint8_t m68681_emulator_get_tx_byte(m68681_channel_t *channel);

/* Modeled interrupt state. The uMac debug device is polled. */
bool m68681_emulator_irq_pending(void);
uint8_t m68681_emulator_interrupt_vector(void);

#endif
