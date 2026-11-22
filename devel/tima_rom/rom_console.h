#ifndef ROM_CONSOLE_H
#define ROM_CONSOLE_H

void rom_console_init(void);
/* Supports %%, %c, %s, %d, %i, %u, %x, %X, %p and l for integers. */
int printf(const char *format, ...);

#endif
