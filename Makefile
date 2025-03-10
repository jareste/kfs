NAME = jareste_kfs.iso
RELEASE_NAME = jareste_kfs_release.iso

BIN_NAME = kernel.bin
BIN_PATH = ./iso/boot/kernel.bin

CC = gcc
AS = nasm
# CFLAGS = -m32 -ffreestanding -nostdlib -nodefaultlibs -fno-builtin -fno-exceptions -fno-stack-protector -O3
CFLAGS = -m32 -ffreestanding -nostdlib -nodefaultlibs -fno-builtin -fno-exceptions -fno-stack-protector -g
ASFLAGS = -f elf
LDFLAGS = -m elf_i386

SRC_DIR = srcs
BOOT_DIR = srcs/boot
LINKER_DIR = linker
OBJ_DIR = objs
ISO_DIR = iso
GRUB_DIR = $(ISO_DIR)/boot/grub

vpath %.c $(SRC_DIR) $(SRC_DIR)/utils $(SRC_DIR)/display $(SRC_DIR)/keyboard $(SRC_DIR)/gdt \
			$(SRC_DIR)/idt $(SRC_DIR)/kshell $(SRC_DIR)/io $(SRC_DIR)/time $(SRC_DIR)/memory \
			$(SRC_DIR)/syscalls $(SRC_DIR)/tasks $(SRC_DIR)/sockets $(SRC_DIR)/ide \
			$(SRC_DIR)/umgmnt $(SRC_DIR)/user/ushell $(SRC_DIR)/display/tty $(SRC_DIR)/modules

vpath %.asm $(BOOT_DIR) $(SRC_DIR)/keyboard $(SRC_DIR)/gdt $(SRC_DIR)/utils $(SRC_DIR)/tasks \
			$(SRC_DIR)/user/syscalls

C_SOURCES = kernel.c strcmp.c strlen.c printf.c putc.c puts.c keyboard.c \
			idt.c itoa.c gdt.c put_hex.c kdump.c kshell.c memset.c strtol.c \
			hatoi.c get_stack_pointer.c kpanic.c dump_registers_c.c \
			io.c init_timers.c memory.c put_zu.c pmm.c memcpy.c memcmp.c \
			interrupts.c signals.c syscalls.c get_line.c layouts.c \
			scheduler.c socket.c queue.c ide.c ext2.c users.c sha256.c \
			strcpy.c users_api.c strncpy.c strncat.c strrchr.c \
			strtok.c strcspn.c strspn.c strcat.c ushell.c env.c \
			strchr.c memmove.c uitoa.c vstrdup.c kstrdup.c sock_registers.c \
			tty.c modules.c mod_keyboard.c mod_time.c

ASM_SOURCES = boot.asm handler.asm gdt_asm.asm dump_registers.asm \
			  clear_registers.asm tasks.asm write.asm kill.asm \
			  read.asm signal.asm get_pid.asm sys_yeld.asm exit.asm \
			  close.asm open.asm sleep.asm fork.asm time.asm usleep.asm

SRC = $(C_SOURCES) $(ASM_SOURCES)

OBJ = $(addprefix $(OBJ_DIR)/, $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o))

DEP = $(addsuffix .d, $(basename $(OBJ)))

all: $(BIN_NAME)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -MMD $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) -o $@ $<

$(BIN_NAME): $(OBJ) $(LINKER_DIR)/link.ld
	@mkdir -p $(GRUB_DIR)
	ld $(LDFLAGS) -T $(LINKER_DIR)/link.ld -o kernel.bin $(OBJ)

build_iso: $(BIN_NAME)
	cp kernel.bin $(ISO_DIR)/boot/
	cp srcs/boot/grub.cfg $(GRUB_DIR)/.
	grub-mkrescue -o $(BIN_PATH) $(ISO_DIR)/


release: $(OBJ) $(LINKER_DIR)/link.ld
	@mkdir -p $(GRUB_DIR)
	ld $(LDFLAGS) -T $(LINKER_DIR)/link.ld -o kernel.bin $(OBJ)
	cp kernel.bin $(ISO_DIR)/boot/
	cp srcs/boot/grub.cfg $(GRUB_DIR)/.
	grub-mkrescue -o $(RELEASE_NAME) $(ISO_DIR) --compress=xz
	
clean:
	rm -f $(OBJ) $(DEP)
	rm -rf $(OBJ_DIR)
	@echo "OBJECTS REMOVED"

fclean: clean
	rm -f kernel.bin $(NAME) $(RELEASE_NAME)
	rm -rf $(ISO_DIR)
	@echo "EVERYTHING REMOVED"

re: fclean all

run:
	qemu-system-i386 -kernel kernel.bin -drive file=disk.img,if=ide,index=0,media=disk,format=raw

	# qemu-system-i386 -kernel $(BIN_NAME) #-m 4096

debug:
	qemu-system-i386 -kernel $(BIN_NAME) -s -S -drive file=disk.img,if=ide,index=0,media=disk,format=raw

run_debug:
	qemu-system-i386 -kernel $(BIN_NAME) -d int,cpu_reset #-m 4096

run_grub: build_iso
	qemu-system-i386 -cdrom $(NAME)

run_release: release
	qemu-system-i386 -cdrom $(RELEASE_NAME)

m_debug:
	qemu-system-i386 -cdrom $(NAME) -d int,cpu_reset

xorriso:
	xorriso -indev $(NAME) -ls /boot/grub/

crhello:
	gcc -nostdlib -nostartfiles -Os -s -ffunction-sections -fdata-sections \
	-Wl,--gc-sections -fno-asynchronous-unwind-tables -fno-pie -no-pie \
	-o hello test/hello.c

crdisk:
	qemu-img create -f raw disk.img 10M
# /usr/sbin/mkfs.ext2 -F -b 1024 -I 128 disk.img
	/usr/sbin/mkfs.ext2 -F -b 1024 -I 128 -g 8192 disk.img

# dd if=/dev/zero of=disk.img bs=512 count=20480

format: crdisk
	- mkdir mnt_ext2
	cc ext2_format.c -o ext2_format
	cp config/users.config .
	echo "holaaaa" >> hello124.txt
	sudo mount -o loop disk.img mnt_ext2
	sudo cp hello124.txt mnt_ext2/
	# sudo mkdir mnt_ext2/etc
	# sudo cp users.config mnt_ext2/etc/.
	sudo cp users.config mnt_ext2/.
	sudo rm -rf lost+found
	sudo umount mnt_ext2
	rm -rf mnt_ext2
# sudo ./ext2_format disk.img users.config hello124.txt
	rm ext2_format users.config hello124.txt

.PHONY: all clean fclean re run xorriso release run_release run_grub debug build_iso

-include $(DEP)
