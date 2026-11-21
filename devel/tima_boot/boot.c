extern volatile unsigned char __mailbox[];

static const unsigned char marker[] = "ROM_TEST_PASS";
volatile unsigned long boot_cookie = 0x13579bdfUL;
volatile unsigned long boot_count;

unsigned long tima_boot(void)
{
    unsigned int i;

    if (boot_cookie != 0x13579bdfUL || boot_count != 0)
        return 1;

    boot_cookie = 0;
    boot_count = 1;
    for (i = 0; i < sizeof marker; ++i)
        __mailbox[i] = marker[i];
    return 0;
}
