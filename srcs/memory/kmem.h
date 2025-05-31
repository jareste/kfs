// kmem.h
#ifndef KMEM_H
#define KMEM_H

#include "../utils/utils.h"
#include "../utils/stdint.h"

// Allocate at least `n` bytes; returns NULL on OOM
void *kmalloc(size_t n);

// Free a pointer previously returned by kmalloc
void  kfree(void *ptr);

// Return the size (in bytes) of the allocation at ptr
size_t ksize(void *ptr);

#endif // KMEM_H
