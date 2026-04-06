### Requirements

- **GCC** or **Clang**
- **NASM** (for assembly code)
- **QEMU** (for testing the kernel)

### Build Instructions

1. **Clone the repository:**
   ```bash
   git clone https://github.com/jareste/kfs.git
   cd kfs

2. **Build the kernel:**
   ```bash
   make

3. **Create the disk image:**
   ```bash
   make format

4. **Run with QEMU:**
   ```bash
   make run

I mainly followed [OSDev](https://wiki.osdev.org/Expanded_Main_Page) docs

