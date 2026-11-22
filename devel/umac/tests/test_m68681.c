#include <stdio.h>
#include <stdlib.h>

#include "cpu_cb.h"
#include "umac.h"
#ifdef UMAC_DEVELOPMENT_DUART
#include "m68681_emulator.h"
#endif

#define CHECK(expr) do {                                                        \
        if (!(expr)) {                                                          \
                fprintf(stderr, "%s:%d: FAIL %s\n", __FILE__, __LINE__, #expr);  \
                exit(EXIT_FAILURE);                                             \
        }                                                                       \
} while (0)

#define SR_A  0xc00003u
#define CR_A  0xc00005u
#define THR_A 0xc00007u
#define IMR   0xc0000bu
#define MR_B  0xc00011u
#define SR_B  0xc00013u
#define CR_B  0xc00015u
#define DATA_B 0xc00017u
#define IVR   0xc00019u
#define TX_READY 0x04u

static const uint8_t rom[ROM_SIZE] = {
        0x00, 0x70, 0x10, 0x00, 0x00, 0x40, 0x00, 0x08,
#ifndef UMAC_DEVELOPMENT_DUART
        /* move.w 0xc00018,%d0 uses the normal unmapped word-read path. */
        0x30, 0x39, 0x00, 0xc0, 0x00, 0x18,
#endif
        0x4e, 0x72, 0x27, 0x00
};
static uint8_t ram[RAM_SIZE];

#ifdef UMAC_DEVELOPMENT_DUART
int main(void)
{
        FILE *console = tmpfile();
        CHECK(console && umac_init(ram, rom, NULL) == 0);
        CHECK(cpu_read_byte(IVR) == 0x0f);
        cpu_write_byte(IVR, 0x70);
        umac_debug_uart(console);
        CHECK(m68681_emulator_interrupt_vector() == 0x70);

        CHECK(cpu_read_byte(0xc00000u) == 0xff);
        CHECK(cpu_read_byte(0xc00001u) == 0);
        CHECK(cpu_read_byte(0xc0001fu) == 0);
        CHECK(cpu_read_byte(0xc00020u) == 0);
        cpu_write_byte(IVR + 0x20, 0x55);
        CHECK(cpu_read_byte(IVR) == 0x70);

        cpu_write_byte(MR_B, 0x13);
        cpu_write_byte(MR_B, 0x07);
        cpu_write_byte(CR_B, 0x10);
        CHECK(cpu_read_byte(MR_B) == 0x13);
        CHECK(cpu_read_byte(MR_B) == 0x07);
        cpu_write_byte(CR_B - 1, 4);
        cpu_write_word(CR_B - 1, 4);
        cpu_write_long(CR_B - 1, 0x00040041u);
        CHECK(cpu_read_byte(SR_B) == 0);

        m68681_channel_t *uart_b = m68681_emulator_get_channel(M68681_EMULATOR_CHANNEL_B);
        CHECK(!m68681_emulator_rx_can_accept(uart_b));
        cpu_write_byte(CR_B, 0x01);
        cpu_write_byte(IMR, 0x20);
        CHECK(m68681_emulator_rx_can_accept(uart_b));
        CHECK(!m68681_emulator_irq_pending());
        CHECK(m68681_emulator_send_rx_byte(uart_b, 'X'));
        CHECK(m68681_emulator_irq_pending());
        CHECK(cpu_read_word(DATA_B - 1) == 0xffff);
        CHECK(cpu_read_long(DATA_B - 1) == UINT32_MAX);
        CHECK(cpu_read_byte(DATA_B) == 'X');
        CHECK(!m68681_emulator_irq_pending());

        cpu_write_byte(CR_A, 4);
        cpu_write_byte(CR_B, 4);
        CHECK(cpu_read_byte(SR_B) & TX_READY);
        cpu_write_byte(THR_A, 'A');
        cpu_write_byte(DATA_B, 0);
        cpu_write_byte(DATA_B, '!');
        CHECK(!(cpu_read_byte(SR_B) & TX_READY));
        CHECK(umac_loop() == 0);
        CHECK(cpu_read_byte(SR_A) & TX_READY);
        CHECK(cpu_read_byte(SR_B) & TX_READY);
        cpu_write_byte(DATA_B, 0xfe);
        CHECK(umac_loop() == 0);

        cpu_write_byte(DATA_B, 'R');
        umac_reset();
        CHECK(cpu_read_byte(IVR) == 0x0f && cpu_read_byte(SR_B) == 0);
        CHECK(umac_loop() == 0);

        cpu_write_byte(IVR, 0x71);
        cpu_write_byte(CR_B, 4);
        umac_debug_uart(NULL);
        CHECK(cpu_read_byte(IVR) == 0x71);
        cpu_write_byte(DATA_B, 'D');
        CHECK(!(cpu_read_byte(SR_B) & TX_READY));
        CHECK(umac_loop() == 0);
        CHECK(cpu_read_byte(SR_B) & TX_READY);

        rewind(console);
        CHECK(fgetc(console) == 0 && fgetc(console) == 0xfe);
        CHECK(fgetc(console) == EOF && feof(console));
        CHECK(fclose(console) == 0);
        puts("MC68681 window, byte lanes, RX, UART-B sink and reset passed");
        return EXIT_SUCCESS;
}
#else
int main(void)
{
        CHECK(umac_init(ram, rom, NULL) == 0);
        cpu_write_byte(IVR, 0x70);
        cpu_write_byte(CR_B, 4);
        cpu_write_byte(DATA_B, 'B');
        for (unsigned int address = 0xc00000; address < 0xc00020; ++address)
                CHECK(cpu_read_byte(address) == 0);
        CHECK(umac_loop() == 1);
        puts("Production map leaves all 32 DUART bytes unmapped");
        return EXIT_SUCCESS;
}
#endif
