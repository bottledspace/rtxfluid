#include "shmem.h"
#include <windows.h>
#include <stdio.h>
#include <assert.h>

#define SHMEM_MAX (1024ull*1024ull*1024ull*4ull)  // 4 gigs


void shmem_alloc(struct shmem_MemoryBlock *block)
{
    block->file = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_RESERVE,
        SHMEM_MAX >> 31, (DWORD)SHMEM_MAX, block->name);
    assert(block->file);
    block->view = MapViewOfFile(block->file, FILE_MAP_WRITE, 0, 0, 0);
    assert(block->view);
    VirtualAlloc((LPBYTE)block->view, block->size, MEM_COMMIT, PAGE_READWRITE);
    
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // If the memory already exists, we map enough to get the true size and then resize to that.
        fprintf(stderr, "INFO: Attaching to block %s with size %lld.\n", block->name, block->size);
    } else {
        // If we actually created the memory, record its true size for other processes.
        fprintf(stderr, "INFO: Creating block %s with size %lld.\n", block->name, block->size);
    }
}


void shmem_grow(struct shmem_MemoryBlock *block)
{
    // Double the size of the region of memory.
    block->size *= 2;
    VirtualAlloc((LPBYTE)block->view, block->size, MEM_COMMIT, PAGE_READWRITE);
}


void shmem_free(struct shmem_MemoryBlock *block)
{
    UnmapViewOfFile(block->view);
    CloseHandle(block->file);
}

