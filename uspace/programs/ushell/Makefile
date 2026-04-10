NAME = ushell

KFS_STDLIB_PATH = ../../stdlib

CC = cc
AS = nasm
CFLAGS = -m32 -static -nostdlib -nostartfiles -ffreestanding -fno-pic -fno-pie -Wl,--build-id=none
ASMFLAGS = -f elf32 
# LDFLAGS = -m elf_i386 -static -nostdlib -nostartfiles -ffreestanding -fno-pic -fno-pie

SRC_DIR = syscalls
vpath %.c $(SRC_DIR)

vpath %.asm $(SRC_DIR)

FILES_ASM = 
FILES_C = ushell

SRCS_ASM = $(addsuffix .asm, $(FILES_ASM))
SRCS_C = $(addsuffix .c, $(FILES_C))

OBJ_DIR = objs
OBJS = $(addprefix $(OBJ_DIR)/, $(SRCS_C:.c=.o) $(SRCS_ASM:.asm=.o))

DEP = $(addsuffix .d, $(basename $(OBJS)))

.PHONY: all clean fclean re .gitignore

all: .gitignore $(NAME)

$(NAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -L$(KFS_STDLIB_PATH) -lkfs_stdlib -o $(NAME)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -MMD $(CFLAGS) -I$(KFS_STDLIB_PATH) -c $< -o $@

$(OBJ_DIR)/%.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASMFLAGS) -o $@ $<

clean:
	rm -rf $(OBJS)

fclean: clean
	rm -f $(NAME)

.gitignore:
	@if [ ! -f .gitignore ]; then \
		echo ".gitignore not found, creating it..."; \
		echo ".gitignore" >> .gitignore; \
		echo "$(NAME)" >> .gitignore; \
		echo "$(OBJ_DIR)/" >> .gitignore; \
		echo "*.o" >> .gitignore; \
		echo "*.a" >> .gitignore; \
		echo "*.pem" >> .gitignore; \
		echo ".vscode/" >> ../.gitignore; \
		echo ".gitignore" >> ../.gitignore; \
		echo "log.txt" >> ../.gitignore; \
		echo ".gitignore created and updated with entries."; \
	else \
		echo ".gitignore already exists."; \
	fi

re: fclean all
