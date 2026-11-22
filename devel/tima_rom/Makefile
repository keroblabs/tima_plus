TOOLCHAIN ?= /home/marcio/Projects/languages/vbcc_pack/vport/build/host/bin
BUILD ?= build
PYTHON ?= python3

VBCC = $(TOOLCHAIN)/vbccm68k
AS68K = $(TOOLCHAIN)/vasmm68k_std
VLINK = $(TOOLCHAIN)/vlink
UMAC = $(BUILD)/umac/umac
LINK = "$(VLINK)" -Bstatic -nostdlib -T m0001ap-rom.ld
OBJS = $(BUILD)/crt0.o $(BUILD)/boot.o $(BUILD)/rom_console.o

.PHONY: all test umac
.DELETE_ON_ERROR:
all: $(BUILD)/tima-boot.rom $(BUILD)/tima-boot.elf

$(BUILD):
	mkdir -p "$@"

$(BUILD)/boot.s $(BUILD)/rom_console.s: $(BUILD)/%.s: %.c rom_console.h $(VBCC) | $(BUILD)
	"$(VBCC)" -quiet -cpu=68000 -gas -elf -rodata -o="$@" "$<"

$(BUILD)/%.o: $(BUILD)/%.s $(AS68K)
	"$(AS68K)" -quiet -gas -m68000 -elfregs -Felf -o "$@" "$<"

$(BUILD)/crt0.o: crt0.s $(AS68K) | $(BUILD)
	"$(AS68K)" -quiet -gas -m68000 -elfregs -Felf -o "$@" "$<"

$(BUILD)/tima-boot.raw: $(OBJS) m0001ap-rom.ld $(VLINK)
	$(LINK) -b rawbin1 -M"$(BUILD)/tima-boot.map" -o "$@" $(OBJS)

$(BUILD)/tima-boot.elf: $(OBJS) m0001ap-rom.ld $(VLINK)
	$(LINK) -b elf32m68k -o "$@" $(OBJS)

$(BUILD)/tima-boot.rom: $(BUILD)/tima-boot.raw rom_test.py
	$(PYTHON) rom_test.py pack "$<" "$@"

umac:
	cmake -S ../umac -B "$(BUILD)/umac" -DMEMSIZE=4096 -DBUILD_TESTING=OFF
	cmake --build "$(BUILD)/umac" --target umac --parallel 2

test: all umac
	$(PYTHON) rom_test.py run "$(UMAC)" "$(BUILD)/tima-boot.rom"
