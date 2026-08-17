NAME = jareste_kfs.iso
RELEASE_NAME = jareste_kfs_release.iso

BIN_NAME = kernel.bin
BIN_PATH = ./iso/boot/kernel.bin

CC = gcc
AS = nasm
# CFLAGS  = -m32 -ffreestanding -nostdlib -nodefaultlibs \
#            -fno-builtin -fno-exceptions -fno-stack-protector \
#            -O0 -Wall -Wextra
CFLAGS  = -m32 -ffreestanding -nostdlib -nodefaultlibs \
			-fno-builtin -fno-exceptions -fno-stack-protector \
			-O0 -Wall -Wextra -g
# ASFLAGS = -f elf
ASFLAGS = -f elf -g
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
			$(SRC_DIR)/umgmnt $(SRC_DIR)/display/tty $(SRC_DIR)/modules \
			$(SRC_DIR)/panic

vpath %.asm $(BOOT_DIR) $(SRC_DIR)/keyboard $(SRC_DIR)/gdt $(SRC_DIR)/utils $(SRC_DIR)/tasks \

C_SOURCES = kernel.c strcmp.c strlen.c kprintf.c putc.c puts.c keyboard.c \
			idt.c itoa.c gdt.c put_hex.c kdump.c kshell.c memset.c strtol.c \
			hatoi.c get_stack_pointer.c kpanic.c dump_registers_c.c \
			io.c init_timers.c put_zu.c pmm.c vmm.c kmalloc.c vmalloc.c memcpy.c memcmp.c \
			interrupts.c signals.c syscalls.c get_line.c layouts.c \
			scheduler.c socket.c queue.c ide.c ext2.c users.c sha256.c \
			strcpy.c users_api.c strncpy.c strncat.c strrchr.c \
			strtok.c strcspn.c strspn.c strcat.c env.c \
			strchr.c memmove.c uitoa.c vstrdup.c kstrdup.c sock_registers.c \
			tty.c modules.c mod_keyboard.c mod_time.c elf_loader.c elf_module.c \
			task_offsets.c

ASM_SOURCES = boot.asm handler.asm gdt_asm.asm dump_registers.asm \
			  clear_registers.asm tasks.asm

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

# run:
# 	qemu-system-i386 -kernel kernel.bin -drive file=disk.img,if=ide,index=0,media=disk,format=raw

# 	# qemu-system-i386 -kernel $(BIN_NAME) #-m 4096

# debug:
# 	qemu-system-i386 -kernel $(BIN_NAME) -s -S -drive file=disk.img,if=ide,index=0,media=disk,format=raw

run:
	qemu-system-i386 -kernel kernel.bin -drive file=disk.img,if=ide,index=0,media=disk,format=raw -display curses

debug:
	qemu-system-i386 -kernel $(BIN_NAME) -drive file=disk.img,if=ide,index=0,media=disk,format=raw -display curses -s -S

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

SYSROOT_LIBC = sysroot/lib/libc.a
BUSYBOX_BIN = busybox

$(SYSROOT_LIBC): install_musl.sh third-party/musl-1.2.6.tar.gz
	./install_musl.sh

$(BUSYBOX_BIN): install_busybox.sh third-party/busybox-1.36.1.tar.bz2 third-party/busybox-1.36.1.config $(SYSROOT_LIBC)
	./install_busybox.sh

format: crdisk $(BUSYBOX_BIN)
	- mkdir mnt_ext2
	cc ext2_format.c -o ext2_format
	cp config/users.config .
	echo "holaaaa" >> hello124.txt
	sudo mount -o loop disk.img mnt_ext2
	sudo cp hello124.txt mnt_ext2/
	# sudo mkdir mnt_ext2/etc
	# sudo cp users.config mnt_ext2/etc/.
	sudo make -C uspace/stdlib
	gcc -m32 -nostdlib -static -I$(CURDIR)/sysroot/include $(CURDIR)/sysroot/lib/crt1.o $(CURDIR)/sysroot/lib/crti.o test/hello.c $(CURDIR)/sysroot/lib/libc.a $(CURDIR)/sysroot/lib/crtn.o -o hello
	gcc -m32 -nostdlib -static -I$(CURDIR)/sysroot/include $(CURDIR)/sysroot/lib/crt1.o $(CURDIR)/sysroot/lib/crti.o test/hello2.c $(CURDIR)/sysroot/lib/libc.a $(CURDIR)/sysroot/lib/crtn.o -o hello2
	sudo cp hello mnt_ext2/.
	sudo cp busybox mnt_ext2/.
	sudo cp hello2 mnt_ext2/.
	sudo make -C uspace/programs/ushell
	sudo cp uspace/programs/ushell/ushell mnt_ext2/.
	gcc -m32 -c -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -nostdlib -O0  uspace/modules/kb_mod.c -o kb_mod.o
	sudo cp kb_mod.o mnt_ext2/.
	sudo cp users.config mnt_ext2/.
	sudo rm -rf lost+found
	sudo umount mnt_ext2
	rm -rf mnt_ext2
	rm -rf hello hello2 ushell kb_mod.o
	rm ext2_format users.config hello124.txt

.PHONY: all clean fclean re run xorriso release run_release run_grub debug build_iso

-include $(DEP)
