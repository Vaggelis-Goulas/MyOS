CROSS = $(HOME)/opt/cross/bin/
CC    = $(CROSS)i686-elf-gcc
LD    = $(CROSS)i686-elf-ld
OBJCOPY = $(CROSS)i686-elf-objcopy
NASM  = nasm

BUILD = build
BOOT  = boot
SRC   = C

CFLAGS = -ffreestanding -m32 -nostdlib -nostartfiles -nodefaultlibs \
         -fno-pic -fno-asynchronous-unwind-tables -Qn -O2 -Wall \
         -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
         -MMD -MP

C_SOURCES = $(wildcard $(SRC)/*.c)
C_OBJECTS = $(patsubst $(SRC)/%.c,$(BUILD)/%.o,$(C_SOURCES))

ASM_OBJECTS = $(BUILD)/isr_stubs.o $(BUILD)/task_switch.o

# ---- Userland native executables (32-bit ELF, linked at USER_BASE) ----
USER_DIR   = user
USER_BUILD = $(BUILD)/user
USER_LD    = $(USER_DIR)/user.ld
USER_HDR   = $(USER_DIR)/userlib.h

USER_CFLAGS = -ffreestanding -m32 -nostdlib -nostartfiles -nodefaultlibs \
         -fno-pic -fno-asynchronous-unwind-tables -O2 -Wall \
         -I$(USER_DIR) \
         -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
         -MMD -MP

USER_SOURCES = $(wildcard $(USER_DIR)/*.c)
USER_PROGS   = $(patsubst $(USER_DIR)/%.c,$(USER_BUILD)/%.elf,$(USER_SOURCES))

QEMU = qemu-system-i386
QEMU_MONITOR_PORT ?= 55555
# Nothing is switched off here. Serial keeps stdio so a session can be
# captured with tee, and the monitor is moved onto a TCP port rather than
# being disabled with "-monitor none", since only one thing can own stdio.
# Reach it with:  telnet 127.0.0.1 $(QEMU_MONITOR_PORT)
QEMU_FLAGS = -drive file=$(BUILD)/floppy.img,format=raw,if=floppy \
             -drive file=disk.img,format=raw,if=ide \
             -serial stdio -display sdl \
             -monitor telnet:127.0.0.1:$(QEMU_MONITOR_PORT),server,nowait

.PHONY: all clean run test test-console disk user

all: $(BUILD)/floppy.img $(USER_PROGS)

user: $(USER_PROGS)

disk: disk.img

disk.img: tools/build_fat12.py $(USER_PROGS)
	python3 tools/build_fat12.py $@ $(USER_PROGS)

test: all
	@rm -f disk.img
	@$(MAKE) --no-print-directory disk
	python3 tools/qemu_test.py

# Console behaviour: scrollback and line wrapping. Kept apart from the
# filesystem/shell suite above, which only ever produces short output and so
# never scrolls the screen.
test-console: all
	python3 tools/console_test.py

$(BUILD)/boot.bin: $(BOOT)/boot.asm
	$(NASM) -f bin $(BOOT)/boot.asm -o $@

$(BUILD)/stage2.bin: $(BOOT)/stage2.asm
	$(NASM) -f bin $(BOOT)/stage2.asm -o $@

$(BUILD)/isr_stubs.o: $(BOOT)/isr_stubs.asm
	$(NASM) -f elf32 $(BOOT)/isr_stubs.asm -o $@

$(BUILD)/task_switch.o: $(BOOT)/task_switch.asm
	$(NASM) -f elf32 $(BOOT)/task_switch.asm -o $@

$(BUILD)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Standalone userland ELF: native 32-bit executable for MyOS user space.
# Linked with user/user.ld at USER_BASE (0x00400000), entry _start(argc,argv).
$(USER_BUILD)/%.elf: $(USER_DIR)/%.c $(USER_LD) $(USER_HDR)
	mkdir -p $(USER_BUILD)
	$(CC) $(USER_CFLAGS) -T $(USER_LD) -e _start $< -o $@

$(BUILD)/kernel.elf: $(C_OBJECTS) $(ASM_OBJECTS) $(SRC)/linker.ld
	$(LD) -T $(SRC)/linker.ld -m elf_i386 -nostdlib \
	    $(C_OBJECTS) $(ASM_OBJECTS) -o $@

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/floppy.img: $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin
	dd if=/dev/zero of=$@ bs=512 count=2880
	dd if=$(BUILD)/boot.bin of=$@ conv=notrunc
	dd if=$(BUILD)/stage2.bin of=$@ bs=512 seek=1 conv=notrunc
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=2 conv=notrunc

clean:
	rm -f $(BUILD)/*.o $(BUILD)/*.d $(BUILD)/*.elf $(BUILD)/*.bin $(BUILD)/*.img
	rm -rf $(USER_BUILD)

run: all
	$(QEMU) $(QEMU_FLAGS)

# Header dependencies emitted by -MMD, so editing a .h rebuilds what uses it.
DEPS = $(C_OBJECTS:.o=.d) $(wildcard $(USER_BUILD)/*.d)
-include $(DEPS)