#include "rom_console.h"

#define BOOT_COOKIE 0x13579bdfUL

volatile unsigned long boot_cookie = BOOT_COOKIE;
volatile unsigned long boot_count;

void tima_boot(void)
{
    unsigned long cookie = boot_cookie;
    unsigned long count = boot_count;

    rom_console_init();
    printf("Tima Boot: cookie=0x%lx bss=%lu\n", cookie, count);
    if (cookie != BOOT_COOKIE || count != 0) {
        printf("ROM_TEST_FAIL\n");
        return;
    }
    printf("ROM_TEST_%s\n", "PASS");
}

void tima_boot_trap(void)
{
    rom_console_init();
    printf("\nROM_TEST_TRAP\n");
}
