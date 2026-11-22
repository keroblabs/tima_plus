# Makefile for umac
#
# Builds the sibling Musashi checkout and the SDL2 test application.
#
# Copyright 2024 Matt Evans
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

DEBUG ?= 0
MEMSIZE ?= 4096

SOURCES = $(filter-out src/disc.c,$(wildcard src/*.c))

MUSASHI = ../Musashi
MUSASHI_BUILD = build/musashi
MUSASHI_SRC = m68kcpu.c m68kdasm.c m68kops.c softfloat/softfloat.c
MY_OBJS = $(patsubst %.c, %.o, $(SOURCES))
MUSASHI_OBJS = $(addprefix $(MUSASHI_BUILD)/,$(MUSASHI_SRC:.c=.o))
OBJS = $(MY_OBJS) $(MUSASHI_OBJS)

SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)

LINKFLAGS =
LIBS = $(SDL_LIBS) -lm

INCLUDEFLAGS = -Iinclude/ -I$(MUSASHI) $(SDL_CFLAGS) -DMUSASHI_CNF='"$(CURDIR)/include/m68kconf.h"'
INCLUDEFLAGS += -DENABLE_DASM=1
INCLUDEFLAGS += -DUMAC_MEMSIZE=$(MEMSIZE)
CFLAGS = $(INCLUDEFLAGS) -Wall -Wextra -pedantic -DSIM

ifeq ($(DEBUG),1)
	CFLAGS += -Og -g -ggdb -DDEBUG
endif

.PHONY: all prepare clean
all:	main

$(MUSASHI)/m68kmake: $(MUSASHI)/m68kmake.c
	$(MAKE) -C $(MUSASHI) m68kmake

$(MUSASHI)/%ops.c $(MUSASHI)/%ops.h: $(MUSASHI)/%make $(MUSASHI)/%_in.c
	$< $(MUSASHI) $(word 2,$^)

prepare:	$(MUSASHI)/m68kops.c $(MUSASHI)/m68kops.h

$(OBJS): Makefile include/m68kconf.h
$(MUSASHI_OBJS): $(MUSASHI)/m68kops.h
src/main.o src/m68681_emulator.o: src/m68681_emulator.h

$(MUSASHI_BUILD)/%.o: $(MUSASHI)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

main:	$(OBJS)
	@echo Linking $(OBJS)
	$(CC) $(LINKFLAGS) $^ $(LIBS) -o $@

clean:
	rm -f $(OBJS) main

################################################################################
# Mac driver sources (no need to generally rebuild
# Needs a m68k-linux-gnu binutils, but not GCC.

M68K_CROSS ?= m68k-linux-gnu-
M68K_AS = $(M68K_CROSS)as
M68K_LD = $(M68K_CROSS)ld
M68K_OBJCOPY = $(M68K_CROSS)objcopy

include/sonydrv.h:	sonydrv.bin
	xxd -i < $< > $@

.PHONY: sonydrv.bin
sonydrv.bin:	macsrc/sonydrv.S
	@# Yum hacky
	cpp $< | $(M68K_AS) -o sonydrv.o
	$(M68K_LD) sonydrv.o -o sonydrv.elf -Ttext=0
	$(M68K_OBJCOPY) sonydrv.elf -O binary --keep-section=.text $@
