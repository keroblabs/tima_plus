#include "rom_console.h"

#define DUART_REG(reg) (*(volatile unsigned char *)(0xc00001UL + 2 * (reg)))
#define DUART_ACR   DUART_REG(0x04)
#define DUART_IMR   DUART_REG(0x05)
#define DUART_MR_B  DUART_REG(0x08)
#define DUART_SR_B  DUART_REG(0x09)
#define DUART_CSR_B DUART_REG(0x09)
#define DUART_CR_B  DUART_REG(0x0a)
#define DUART_THR_B DUART_REG(0x0b)
#define DUART_TX_READY 0x04

void rom_console_init(void)
{
    DUART_IMR = 0;
    DUART_ACR = 0;
    DUART_CR_B = 0x30;  /* Reset transmitter. */
    DUART_CR_B = 0x10;  /* Select MR1. */
    DUART_MR_B = 0x13;  /* 8 data bits, no parity. */
    DUART_MR_B = 0x07;  /* One stop bit, normal mode. */
    DUART_CSR_B = 0xbb; /* 9600 baud. */
    DUART_CR_B = 0x04;  /* Enable transmitter. */
}

static void uart_b_putc(unsigned char byte)
{
    while (!(DUART_SR_B & DUART_TX_READY)) {
    }
    DUART_THR_B = byte;
}

static int print_string(const char *text)
{
    const char *start = text;
    while (*text)
        uart_b_putc(*text++);
    return text - start;
}

static int print_decimal(unsigned long value)
{
    /* The 68000 has no 32-bit divide. Each place takes at most nine subtractions. */
    static const unsigned long places[] = {
        1000000000UL, 100000000UL, 10000000UL, 1000000UL, 100000UL,
        10000UL, 1000UL, 100UL, 10UL, 1UL
    };
    unsigned int i;
    int count = 0;

    for (i = 0; i < sizeof places / sizeof places[0]; ++i) {
        unsigned long place = places[i];
        char digit = '0';
        while (value >= place) {
            value -= place;
            ++digit;
        }
        if (count || digit != '0' || place == 1) {
            uart_b_putc(digit);
            ++count;
        }
    }
    return count;
}

static int print_hex(unsigned long value, const char *alphabet)
{
    int shift, count = 0;
    for (shift = 28; shift >= 0; shift -= 4) {
        unsigned int digit = (value >> shift) & 0x0f;
        if (count || digit || shift == 0) {
            uart_b_putc(alphabet[digit]);
            ++count;
        }
    }
    return count;
}

/* The project's vbccm68k ABI puts int, long and pointers in 32-bit stack slots. */
#define NEXT_ARG(type) (args += sizeof(type), ((type *)args)[-1])

int printf(const char *format, ...)
{
    static const char lower_hex[] = "0123456789abcdef";
    static const char upper_hex[] = "0123456789ABCDEF";
    unsigned char *args = (unsigned char *)(&format + 1);
    int count = 0;

    while (*format) {
        char conversion;
        int long_arg;
        if (*format != '%') {
            uart_b_putc(*format++);
            ++count;
            continue;
        }
        ++format;
        long_arg = *format == 'l';
        if (long_arg)
            ++format;
        conversion = *format++;

        switch (conversion) {
        case 'd':
        case 'i': {
            long value = long_arg ? NEXT_ARG(long) : NEXT_ARG(int);
            unsigned long magnitude = (unsigned long)value;
            if (value < 0) {
                uart_b_putc('-');
                ++count;
                magnitude = 0UL - magnitude;
            }
            count += print_decimal(magnitude);
            break;
        }
        case 'u':
            count += print_decimal(long_arg ? NEXT_ARG(unsigned long) : NEXT_ARG(unsigned int));
            break;
        case 'x':
        case 'X':
            count += print_hex(long_arg ? NEXT_ARG(unsigned long) : NEXT_ARG(unsigned int),
                               conversion == 'x' ? lower_hex : upper_hex);
            break;
        case 's': {
            const char *text;
            if (long_arg)
                return -1;
            text = NEXT_ARG(const char *);
            count += print_string(text ? text : "(null)");
            break;
        }
        case 'p':
            if (long_arg)
                return -1;
            count += print_string("0x");
            count += print_hex((unsigned long)NEXT_ARG(void *), lower_hex);
            break;
        case 'c':
        case '%':
            if (long_arg)
                return -1;
            uart_b_putc(conversion == 'c' ? NEXT_ARG(int) : '%');
            ++count;
            break;
        default:
            return -1;
        }
    }
    return count;
}
