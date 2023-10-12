#include <stdint.h>

struct shmem_MemoryBlock {
    const char *name;
    // === Internal ===
    uint64_t size;
    void *file;
    void *view;
};


void shmem_alloc(struct shmem_MemoryBlock *block);
void shmem_grow(struct shmem_MemoryBlock *block);
void shmem_free(struct shmem_MemoryBlock *block);

// Use this to safely modify shared memory. If the block has grown in another
// process this is also where it will be remapped.
void *shmem_lock(struct shmem_MemoryBlock *block);
void shmem_unlock(struct shmem_MemoryBlock *block);